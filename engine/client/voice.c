/*
voice.c - voice chat implementation
Copyright (C) 2022 Velaron
Copyright (C) 2022 SNMetamorph

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include <opus.h>
#include "common.h"
#include "client.h"
#include "voice.h"

static wavdata_t *input_file;
static fs_offset_t input_pos;

voice_state_t voice = { 0 };

CVAR_DEFINE_AUTO( voice_enable, "1", FCVAR_PRIVILEGED|FCVAR_ARCHIVE, "enable voice chat" );
CVAR_DEFINE_AUTO( voice_loopback, "0", FCVAR_PRIVILEGED, "loopback voice back to the speaker" );
CVAR_DEFINE_AUTO( voice_scale, "1.0", FCVAR_PRIVILEGED|FCVAR_ARCHIVE, "incoming voice volume scale" );
CVAR_DEFINE_AUTO( voice_avggain, "0.5", FCVAR_PRIVILEGED|FCVAR_ARCHIVE, "automatic voice gain control (average)" );
CVAR_DEFINE_AUTO( voice_maxgain, "5.0", FCVAR_PRIVILEGED|FCVAR_ARCHIVE, "automatic voice gain control (maximum)" );
CVAR_DEFINE_AUTO( voice_inputfromfile, "0", FCVAR_PRIVILEGED, "input voice from voice_input.wav" );

static const char* Voice_GetBandwidthTypeName( int bandwidthType )
{
	switch( bandwidthType )
	{
	case OPUS_BANDWIDTH_FULLBAND: return "Full Band (20 kHz)";
	case OPUS_BANDWIDTH_SUPERWIDEBAND: return "Super Wide Band (12 kHz)";
	case OPUS_BANDWIDTH_WIDEBAND: return "Wide Band (8 kHz)";
	case OPUS_BANDWIDTH_MEDIUMBAND: return "Medium Band (6 kHz)";
	case OPUS_BANDWIDTH_NARROWBAND: return "Narrow Band (4 kHz)";
	default: return "Unknown";
	}
}

static void Voice_CodecInfo_f( void )
{
	int encoderComplexity;
	opus_int32 encoderBitrate;
	opus_int32 encoderBandwidthType;

	if( !voice.initialized )
	{
		Con_Printf( "Voice codec is not initialized!\n" );
		return;
	}

	opus_encoder_ctl( voice.encoder, OPUS_GET_BITRATE( &encoderBitrate ));
	opus_encoder_ctl( voice.encoder, OPUS_GET_COMPLEXITY( &encoderComplexity ));
	opus_encoder_ctl( voice.encoder, OPUS_GET_BANDWIDTH( &encoderBandwidthType ));

	Con_Printf( "Encoder:\n" );
	Con_Printf( "  Bitrate: %.3f kbps\n", encoderBitrate / 1000.0f );
	Con_Printf( "  Complexity: %d\n", encoderComplexity );
	Con_Printf( "  Bandwidth: %s", Voice_GetBandwidthTypeName( encoderBandwidthType ));
	Con_Printf( "\n" );
}

void Voice_RegisterCvars( void )
{
	Cvar_RegisterVariable( &voice_enable );
	Cvar_RegisterVariable( &voice_loopback );
	Cvar_RegisterVariable( &voice_scale );
	Cvar_RegisterVariable( &voice_avggain );
	Cvar_RegisterVariable( &voice_maxgain );
	Cvar_RegisterVariable( &voice_inputfromfile );
	Cmd_AddClientCommand( "voice_codecinfo", Voice_CodecInfo_f );
}

static void Voice_Status( int entindex, qboolean bTalking )
{
	clgame.dllFuncs.pfnVoiceStatus( entindex, bTalking );
}

static uint Voice_GetFrameSize( float durationMsec )
{
	return voice.channels * voice.width * (( float )voice.samplerate / ( 1000.0f / durationMsec ));
}

static void Voice_ApplyGainAdjust( opus_int16 *samples, int count )
{
	float gain, modifiedMax;
	int average, adjustedSample;
	int blockOffset = 0;

	for( ;;)
	{
		int i;
		int localMax = 0;
		int localSum = 0;
		int blockSize = Q_min( count - ( blockOffset + voice.autogain.block_size ), voice.autogain.block_size );
		
		if( blockSize < 1 )
			break;

		for( i = 0; i < blockSize; ++i )
		{
			int sample = samples[blockOffset + i];
			if( abs( sample ) > localMax ) {
				localMax = abs( sample );
			}
			localSum += sample;

			gain = voice.autogain.current_gain + i * voice.autogain.gain_multiplier;
			adjustedSample = Q_min( 32767, Q_max(( int )( sample * gain ), -32768 ));
			samples[blockOffset + i] = adjustedSample;
		}

		if( blockOffset % voice.autogain.block_size == 0 )
		{
			average = localSum / blockSize;
			modifiedMax = average + ( localMax - average ) * voice_avggain.value;
			voice.autogain.current_gain = voice.autogain.next_gain * voice_scale.value;
			voice.autogain.next_gain = Q_min( 32767.0f / modifiedMax, voice_maxgain.value ) * voice_scale.value;
			voice.autogain.gain_multiplier = ( voice.autogain.next_gain - voice.autogain.current_gain ) / ( voice.autogain.block_size - 1 );
		}
		blockOffset += blockSize;
	}
}

// parameters currently unused
qboolean Voice_Init( const char *pszCodecName, int quality )
{
	int err;

	if( !voice_enable.value )
		return false;

	Voice_DeInit();

	voice.initialized = false;
	voice.channels = 1;
	voice.width = 2;
	voice.samplerate = SOUND_48k;
	voice.frame_size = Voice_GetFrameSize( 40.0f ); 
	voice.autogain.block_size = 128;

	if( !VoiceCapture_Init() )
	{
		Voice_DeInit();
		return voice.initialized;
	}

	voice.encoder = opus_encoder_create( voice.samplerate, voice.channels, OPUS_APPLICATION_VOIP, &err );

	if( err != OPUS_OK )
		return voice.initialized;

	voice.decoder = opus_decoder_create( voice.samplerate, voice.channels, &err );

	switch( quality )
	{
	case 1: // 4800 bits per second, <4 kHz bandwidth
		opus_encoder_ctl( voice.encoder, OPUS_SET_BITRATE( 4800 ));
		opus_encoder_ctl( voice.encoder, OPUS_SET_BANDWIDTH( OPUS_BANDWIDTH_NARROWBAND ));
		break;
	case 2: // 12000 bits per second, <6 kHz bandwidth
		opus_encoder_ctl( voice.encoder, OPUS_SET_BITRATE( 12000 ));
		opus_encoder_ctl( voice.encoder, OPUS_SET_BANDWIDTH( OPUS_BANDWIDTH_MEDIUMBAND ));
		break;
	case 4: // automatic bitrate, full band (20 kHz)
		opus_encoder_ctl( voice.encoder, OPUS_SET_BITRATE( OPUS_AUTO ));
		opus_encoder_ctl( voice.encoder, OPUS_SET_BANDWIDTH( OPUS_BANDWIDTH_FULLBAND ));
		break;
	case 5: // maximum bitrate, full band (20 kHz)
		opus_encoder_ctl( voice.encoder, OPUS_SET_BITRATE( OPUS_BITRATE_MAX ));
		opus_encoder_ctl( voice.encoder, OPUS_SET_BANDWIDTH( OPUS_BANDWIDTH_FULLBAND ));
		break;
	default: // 36000 bits per second, <12 kHz bandwidth
		opus_encoder_ctl( voice.encoder, OPUS_SET_BITRATE( 36000 ));
		opus_encoder_ctl( voice.encoder, OPUS_SET_BANDWIDTH( OPUS_BANDWIDTH_SUPERWIDEBAND ));
		break;
	}

	voice.initialized = (err == OPUS_OK);
	return voice.initialized;
}

void Voice_DeInit( void )
{
	if( !voice.initialized )
		return;

	Voice_RecordStop();

	opus_encoder_destroy( voice.encoder );
	opus_decoder_destroy( voice.decoder );

	voice.initialized = false;
}

static uint Voice_GetCompressedData( byte *out, uint maxsize, uint *frames )
{
	uint ofs, size = 0;

	if( input_file )
	{
		uint numbytes;
		double updateInterval;

		updateInterval = cl.mtime[0] - cl.mtime[1];
		numbytes = updateInterval * voice.samplerate * voice.width * voice.channels;
		numbytes = Q_min( numbytes, input_file->size - input_pos );
		numbytes = Q_min( numbytes, sizeof( voice.input_buffer ) - voice.input_buffer_pos );

		memcpy( voice.input_buffer + voice.input_buffer_pos, input_file->buffer + input_pos, numbytes );
		voice.input_buffer_pos += numbytes;
		input_pos += numbytes;
	}

	for( ofs = 0; voice.input_buffer_pos - ofs >= voice.frame_size && ofs <= voice.input_buffer_pos; ofs += voice.frame_size )
	{
		int bytes;

		if( !input_file )
		{
			// adjust gain before encoding, but only for input from voice
			Voice_ApplyGainAdjust((opus_int16*)voice.input_buffer + ofs, voice.frame_size);
		}

		bytes = opus_encode( voice.encoder, (const opus_int16*)(voice.input_buffer + ofs), voice.frame_size / voice.width, out + size, maxsize );
		memmove( voice.input_buffer, voice.input_buffer + voice.frame_size, sizeof( voice.input_buffer ) - voice.frame_size );
		voice.input_buffer_pos -= voice.frame_size;

		if( bytes > 0 )
		{
			size += bytes;
			(*frames)++;
		}
	}

	return size;
}

static void Voice_StatusTimeout( voice_status_t *status, int entindex, double frametime )
{
	if( status->talking_ack )
	{
		status->talking_timeout += frametime;
		if( status->talking_timeout > 0.2 )
		{
			status->talking_ack = false;
			Voice_Status( entindex, false );
		}
	}
}

void Voice_StatusAck( voice_status_t *status, int playerIndex )
{
	if( !status->talking_ack )
		Voice_Status( playerIndex, true );

	status->talking_ack = true;
	status->talking_timeout = 0.0;
}

void Voice_Idle( double frametime )
{
	int i;

	if( !voice_enable.value )
	{
		Voice_DeInit();
		return;
	}

	// update local player status first
	Voice_StatusTimeout( &voice.local, VOICE_LOCALPLAYER_INDEX, frametime );

	for( i = 0; i < 32; i++ )
		Voice_StatusTimeout( &voice.players_status[i], i, frametime );
}

qboolean Voice_IsRecording( void )
{
	return voice.is_recording;
}

void Voice_RecordStop( void )
{
	if( input_file )
	{
		FS_FreeSound( input_file );
		input_file = NULL;
	}

	voice.input_buffer_pos = 0;
	memset( voice.input_buffer, 0, sizeof( voice.input_buffer ) );

	if( Voice_IsRecording() )
		Voice_Status( -1, false );
	
	VoiceCapture_RecordStop();
	
	voice.is_recording = false;
}

void Voice_RecordStart( void )
{
	Voice_RecordStop();

	if( voice_inputfromfile.value )
	{
		input_file = FS_LoadSound( "voice_input.wav", NULL, 0 );

		if( input_file )
		{
			Sound_Process( &input_file, voice.samplerate, voice.width, SOUND_RESAMPLE );
			input_pos = 0;
			
			voice.start_time = Sys_DoubleTime();
			voice.is_recording = true;
		}
		else
		{
			FS_FreeSound( input_file );
			input_file = NULL;
		}
	}

	if( !Voice_IsRecording() )
		voice.is_recording = VoiceCapture_RecordStart();

	if( Voice_IsRecording() )
		Voice_Status( -1, true );
}

void Voice_Disconnect( void )
{
	int i;

	Voice_RecordStop();
	for( i = 0; i <= 32; i++ ) {
		Voice_Status( i, false );
	}
}

static void Voice_StartChannel( uint samples, byte *data, int entnum )
{
	SND_ForceInitMouth( entnum );
	S_RawEntSamples( entnum, samples, voice.samplerate, voice.width, voice.channels, data, 255 );
}

void Voice_AddIncomingData( int ent, const byte *data, uint size, uint frames )
{
	if( !voice.initialized )
		return;

	int samples = opus_decode( voice.decoder, data, size, (short *)voice.decompress_buffer, voice.frame_size / voice.width * frames, false );

	if( samples > 0 )
		Voice_StartChannel( samples, voice.decompress_buffer, ent );
}

void CL_AddVoiceToDatagram( void )
{
	uint size, frames = 0;

	if( cls.state != ca_active || !Voice_IsRecording() )
		return;
	
	size = Voice_GetCompressedData( voice.output_buffer, sizeof( voice.output_buffer ), &frames );

	if( size > 0 && MSG_GetNumBytesLeft( &cls.datagram ) >= size + 32 )
	{
		MSG_BeginClientCmd( &cls.datagram, clc_voicedata );
		MSG_WriteByte( &cls.datagram, voice_loopback.value != 0 );
		MSG_WriteByte( &cls.datagram, frames );
		MSG_WriteShort( &cls.datagram, size );
		MSG_WriteBytes( &cls.datagram, voice.output_buffer, size );
	}
}
