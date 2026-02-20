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

#define CUSTOM_MODES 1 // required to correctly link with Opus Custom
#include <opus_custom.h>
#include <opus.h>
#include "common.h"
#include "client.h"
#include "voice.h"
#include "crclib.h"

voice_state_t voice = { 0 };

static CVAR_DEFINE_AUTO( voice_enable, "1", FCVAR_PRIVILEGED | FCVAR_ARCHIVE, "enable voice chat" );
CVAR_DEFINE_AUTO( voice_loopback, "0", FCVAR_PRIVILEGED, "loopback voice back to the speaker" );
static CVAR_DEFINE_AUTO( voice_scale, "1.0", FCVAR_PRIVILEGED | FCVAR_ARCHIVE, "incoming voice volume scale" );
static CVAR_DEFINE_AUTO( voice_transmit_scale, "1.0", FCVAR_PRIVILEGED | FCVAR_ARCHIVE, "outcoming voice volume scale" );
static CVAR_DEFINE_AUTO( voice_avggain, "0.5", FCVAR_PRIVILEGED | FCVAR_ARCHIVE, "automatic voice gain control (average)" );
static CVAR_DEFINE_AUTO( voice_maxgain, "5.0", FCVAR_PRIVILEGED | FCVAR_ARCHIVE, "automatic voice gain control (maximum)" );
static CVAR_DEFINE_AUTO( voice_inputfromfile, "0", FCVAR_PRIVILEGED, "input voice from voice_input.wav" );

static void Voice_StartChannel( uint samples, byte *data, int entnum );

/*
===============================================================================

	UTILITY FUNCTIONS

===============================================================================
*/

/*
=========================
Voice_IsGoldSrcMode

Check if codec is GoldSrc mode
=========================
*/
static qboolean Voice_IsGoldSrcMode( const char *codec )
{
	// check for an empty codec string
	// if the server does not use ReVoice or VTC
	// it will send an empty codec
	// however, decoding is still possible
	// if the voice data contains Opus
	if( COM_StringEmptyOrNULL( codec ))
		return true;

	if( Q_strstr( codec, "voice_speex" ))
		return true;

	return false;
}

/*
=========================
Voice_IsOpusCustomMode

Check if codec is Opus Custom mode
=========================
*/
static qboolean Voice_IsOpusCustomMode( const char *codec )
{
	return Q_strcmp( codec, VOICE_OPUS_CUSTOM_CODEC ) == 0;
}

/*
=========================
Voice_GetBitrateForQuality

Get bitrate for given quality level
=========================
*/
static int Voice_GetBitrateForQuality( int quality, qboolean goldsrc )
{
	switch( quality )
	{
	case 1: return 6000;   // 6 kbps
	case 2: return 12000;  // 12 kbps
	case 3: return 24000;  // 24 kbps
	case 4: return 36000;  // 36 kbps
	case 5: return 48000;  // 48 kbps
	default: return 36000; // default
	}
}

/*
===============================================================================

	OPUS INTEGRATION

===============================================================================
*/

/*
=========================
Voice_InitCustomMode

Initialize Opus Custom mode
=========================
*/
static qboolean Voice_InitCustomMode( void )
{
	int err = 0;

	voice.width = sizeof( int16_t );
	voice.samplerate = VOICE_OPUS_CUSTOM_SAMPLERATE;
	voice.frame_size = VOICE_OPUS_CUSTOM_FRAME_SIZE;

	voice.custom_mode = opus_custom_mode_create( SOUND_44k, voice.frame_size, &err );

	if( !voice.custom_mode )
	{
		Con_Printf( S_ERROR "Can't create Opus Custom mode: %s\n", opus_strerror( err ));
		return false;
	}

	return true;
}

/*
=========================
Voice_InitOpusDecoder

Initialize Opus decoders for clients
=========================
*/
static qboolean Voice_InitOpusDecoder( void )
{
	int err = 0;

	for( int i = 0; i < cl.maxclients; i++ )
	{
		if( voice.goldsrc )
		{
			voice.gs_decoders[i] = opus_decoder_create( GS_DEFAULT_SAMPLE_RATE, VOICE_PCM_CHANNELS, &err );
			if( !voice.gs_decoders[i] )
			{
				Con_Printf( S_ERROR "Can't create GoldSrc Opus decoder for %i: %s\n", i, opus_strerror( err ));
				return false;
			}
		}
		else
		{
			voice.decoders[i] = opus_custom_decoder_create( voice.custom_mode, VOICE_PCM_CHANNELS, &err );
			if( !voice.decoders[i] )
			{
				Con_Printf( S_ERROR "Can't create Custom Opus decoder for %i: %s\n", i, opus_strerror( err ));
				return false;
			}
		}
	}

	return true;
}

/*
=========================
Voice_InitOpusEncoder

Initialize Opus encoder with quality settings
=========================
*/
static qboolean Voice_InitOpusEncoder( int quality )
{
	int err = 0;
	int bitrate = Voice_GetBitrateForQuality( quality, voice.goldsrc );

	if( voice.goldsrc )
	{
		voice.gs_encoder = opus_encoder_create( GS_DEFAULT_SAMPLE_RATE, VOICE_PCM_CHANNELS, OPUS_APPLICATION_VOIP, &err );
		if( !voice.gs_encoder )
		{
			Con_Printf( S_ERROR "Can't create GoldSrc Opus encoder: %s\n", opus_strerror( err ));
			return false;
		}
		opus_encoder_ctl( voice.gs_encoder, OPUS_SET_DTX( 1 ));
		opus_encoder_ctl( voice.gs_encoder, OPUS_SET_BITRATE( bitrate ));
	}
	else
	{
		voice.encoder = opus_custom_encoder_create( voice.custom_mode, VOICE_PCM_CHANNELS, &err );
		if( !voice.encoder )
		{
			Con_Printf( S_ERROR "Can't create Opus encoder: %s\n", opus_strerror( err ));
			return false;
		}
		opus_custom_encoder_ctl( voice.encoder, OPUS_SET_BITRATE( bitrate ));
	}

	return true;
}

/*
=========================
Voice_ShutdownOpusDecoder

Cleanup Opus decoders
=========================
*/
static void Voice_ShutdownOpusDecoder( void )
{
	for( int i = 0; i < MAX_CLIENTS; i++ )
	{
		if( voice.goldsrc )
		{
			if( voice.gs_decoders[i] )
			{
				opus_decoder_destroy( voice.gs_decoders[i] );
				voice.gs_decoders[i] = NULL;
			}
		}
		else
		{
			if( voice.decoders[i] )
			{
				opus_custom_decoder_destroy( voice.decoders[i] );
				voice.decoders[i] = NULL;
			}
		}
	}
}

/*
=========================
Voice_ShutdownOpusEncoder

Cleanup Opus encoder
=========================
*/
static void Voice_ShutdownOpusEncoder( void )
{
	if( voice.goldsrc )
	{
		if( voice.gs_encoder )
		{
			opus_encoder_destroy( voice.gs_encoder );
			voice.gs_encoder = NULL;
		}
	}
	else
	{
		if( voice.encoder )
		{
			opus_custom_encoder_destroy( voice.encoder );
			voice.encoder = NULL;
		}
	}
}

/*
=========================
Voice_ShutdownCustomMode

Cleanup Opus Custom mode
=========================
*/
static void Voice_ShutdownCustomMode( void )
{
	if( voice.custom_mode )
	{
		opus_custom_mode_destroy( voice.custom_mode );
		voice.custom_mode = NULL;
	}
}

/*
=========================
Voice_InitGoldSrcMode

Initialize GoldSrc voice mode
=========================
*/
static qboolean Voice_InitGoldSrcMode( int quality )
{
	voice.goldsrc = true;
	voice.autogain.block_size = 128;
	voice.width = sizeof( int16_t );
	voice.samplerate = GS_DEFAULT_SAMPLE_RATE;
	voice.frame_size = GS_DEFAULT_FRAME_SIZE;

	if( !Voice_InitOpusDecoder())
	{
		Con_Printf( S_ERROR "Can't create GoldSrc decoders, voice chat is disabled.\n" );
		return false;
	}

	if( !Voice_InitOpusEncoder( quality ))
	{
		Con_Printf( S_WARN "Other players will not be able to hear you.\n" );
		return false;
	}

	return true;
}

/*
=========================
Voice_InitOpusCustomMode

Initialize Opus Custom voice mode
=========================
*/
static qboolean Voice_InitOpusCustomMode( int quality )
{
	voice.goldsrc = false;
	voice.autogain.block_size = 128;
	voice.samplerate = VOICE_OPUS_CUSTOM_SAMPLERATE;
	voice.frame_size = VOICE_OPUS_CUSTOM_FRAME_SIZE;
	voice.width = sizeof( int16_t );

	if( !Voice_InitCustomMode() || !Voice_InitOpusDecoder())
	{
		Con_Printf( S_ERROR "Can't create Opus Custom decoders, voice chat is disabled.\n" );
		return false;
	}

	if( !Voice_InitOpusEncoder( quality ))
	{
		Con_Printf( S_WARN "Other players will not be able to hear you.\n" );
		return false;
	}

	return true;
}

/*
=========================
Voice_ShutdownGoldSrcMode

Cleanup GoldSrc mode
=========================
*/
static void Voice_ShutdownGoldSrcMode( void )
{
	Voice_ShutdownOpusDecoder();
	Voice_ShutdownOpusEncoder();
}

/*
=========================
Voice_ShutdownOpusCustomMode

Cleanup Opus Custom mode
=========================
*/
static void Voice_ShutdownOpusCustomMode( void )
{
	Voice_ShutdownOpusDecoder();
	Voice_ShutdownOpusEncoder();
	Voice_ShutdownCustomMode();
}

/*
===============================================================================

	VOICE PROCESSING

===============================================================================
*/

/*
=========================
Voice_ApplyGainAdjust

Apply automatic gain control to voice samples
=========================
*/
static void Voice_ApplyGainAdjust( int16_t *samples, int count, float scale )
{
	float gain, modifiedMax;
	int   average, blockOffset = 0;

	for( ;; )
	{
		int i, localMax = 0, localSum = 0;
		int blockSize = Q_min( count - blockOffset, voice.autogain.block_size );

		if( blockSize < 1 )
			break;

		for( i = 0; i < blockSize; ++i )
		{
			int sample = samples[blockOffset + i];
			int absSample = abs( sample );

			if( absSample > localMax )
				localMax = absSample;

			localSum += absSample;
			gain = voice.autogain.current_gain + i * voice.autogain.gain_multiplier;
			samples[blockOffset + i] = bound( SHRT_MIN, (int)( sample * gain ), SHRT_MAX );
		}

		if( blockOffset % voice.autogain.block_size == 0 )
		{
			average = localSum / blockSize;
			modifiedMax = average + ( localMax - average ) * voice_avggain.value;

			voice.autogain.current_gain = voice.autogain.next_gain * scale;
			voice.autogain.next_gain = Q_min((float)SHRT_MAX / ( modifiedMax > 1 ? modifiedMax : 1 ), voice_maxgain.value ) * scale;
			if( blockSize > 1 )
				voice.autogain.gain_multiplier = ( voice.autogain.next_gain - voice.autogain.current_gain ) / ( blockSize - 1 );
			else
				voice.autogain.gain_multiplier = 0.0f;
		}
		blockOffset += blockSize;
	}
}

/*
=========================
Voice_GetOpusCompressedData

Get compressed voice data for Opus Custom mode
=========================
*/
static uint Voice_GetOpusCompressedData( byte *out, uint maxsize, uint *frames )
{
	uint ofs = 0, size = 0;
	uint frame_size_bytes = voice.frame_size * voice.width;

	if( voice.input_file )
	{
		uint   numbytes;
		double updateInterval, curtime = Platform_DoubleTime();

		updateInterval = curtime - voice.start_time;
		voice.start_time = curtime;

		numbytes = updateInterval * voice.samplerate * voice.width * VOICE_PCM_CHANNELS;
		numbytes = Q_min( numbytes, voice.input_file->size - voice.input_file_pos );
		numbytes = Q_min( numbytes, sizeof( voice.input_buffer ) - voice.input_buffer_pos );

		memcpy( voice.input_buffer + voice.input_buffer_pos, voice.input_file->buffer + voice.input_file_pos, numbytes );
		voice.input_buffer_pos += numbytes;
		voice.input_file_pos += numbytes;
	}

	if( !voice.input_file )
		VoiceCapture_Lock( true );

	for( ofs = 0; voice.input_buffer_pos - ofs >= frame_size_bytes && ofs <= voice.input_buffer_pos; ofs += frame_size_bytes )
	{
		int bytes;

		if( !voice.input_file )
			Voice_ApplyGainAdjust((int16_t *)( voice.input_buffer + ofs ), voice.frame_size, voice_transmit_scale.value );

		bytes = opus_custom_encode( voice.encoder, (const int16_t *)( voice.input_buffer + ofs ),
					    voice.frame_size, out + size + sizeof( uint16_t ), maxsize );

		if( bytes > 0 )
		{
			// write compressed frame size
			*((uint16_t *)&out[size] ) = LittleShort( bytes );

			size += bytes + sizeof( uint16_t );
			maxsize -= bytes + sizeof( uint16_t );

			( *frames )++;
		}
		else
		{
			Con_Printf( S_ERROR "%s: failed to encode frame: %s\n", __func__, opus_strerror( bytes ));
		}
	}

	// did we compress anything? update counters
	if( ofs )
	{
		fs_offset_t remaining = voice.input_buffer_pos - ofs;
		// move remaining samples to the beginning of buffer
		memmove( voice.input_buffer, voice.input_buffer + ofs, remaining );
		voice.input_buffer_pos = remaining;
	}

	if( !voice.input_file )
		VoiceCapture_Lock( false );

	return size;
}

/*
=========================
Voice_GetGSCompressedData

Get compressed voice data for GoldSrc mode
=========================
*/
static uint Voice_GetGSCompressedData( byte *out, uint maxsize, uint *frames )
{
	uint ofs = 0, size = 0;
	const uint      frame_size_samples = GS_DEFAULT_FRAME_SIZE;
	const uint      frame_size_bytes = GS_DEFAULT_FRAME_SIZE * voice.width;
	static uint16_t sequence = 0;

	if( voice.input_file )
	{
		uint   numbytes;
		double updateInterval, curtime = Platform_DoubleTime();

		updateInterval = curtime - voice.start_time;
		voice.start_time = curtime;

		numbytes = updateInterval * voice.samplerate * voice.width * VOICE_PCM_CHANNELS;
		numbytes = Q_min( numbytes, voice.input_file->size - voice.input_file_pos );
		numbytes = Q_min( numbytes, sizeof( voice.input_buffer ) - voice.input_buffer_pos );

		memcpy( voice.input_buffer + voice.input_buffer_pos, voice.input_file->buffer + voice.input_file_pos, numbytes );
		voice.input_buffer_pos += numbytes;
		voice.input_file_pos += numbytes;
	}

	if( !voice.input_file )
		VoiceCapture_Lock( true );

	*frames = 0;

	while( voice.input_buffer_pos - ofs >= frame_size_bytes )
	{
		int     bytes;
		int     is_silence = 1;
		int16_t *samples = (int16_t *)( voice.input_buffer + ofs );

		for( uint i = 0; i < frame_size_samples; ++i )
		{
			if( samples[i] != 0 )
			{
				is_silence = 0;
				break;
			}
		}
		if( is_silence )
		{
			ofs += frame_size_bytes;
			continue;
		}

		if( !voice.input_file )
			Voice_ApplyGainAdjust( samples, frame_size_samples, voice_transmit_scale.value );

		bytes = opus_encode( voice.gs_encoder, samples,
				     frame_size_samples, out + size + 4, maxsize - 4 );

		if( bytes > 0 )
		{
			*(uint16_t *)( out + size ) = LittleShort( bytes );
			*(uint16_t *)( out + size + 2 ) = LittleShort( sequence );
			sequence++;

			size += bytes + sizeof( uint32_t );
			maxsize -= bytes + sizeof( uint32_t );

			( *frames )++;
		}
		else
		{
			Con_Printf( S_ERROR "%s: failed to encode frame: %s\n", __func__, opus_strerror( bytes ));
		}

		ofs += frame_size_bytes;
	}

	if( ofs )
	{
		fs_offset_t remaining = voice.input_buffer_pos - ofs;
		memmove( voice.input_buffer, voice.input_buffer + ofs, remaining );
		voice.input_buffer_pos = remaining;
	}

	if( !voice.input_file )
		VoiceCapture_Lock( false );

	return size;
}

/*
=========================
Voice_ProcessGSData

Process GoldSrc voice data and return number of samples
=========================
*/
static int Voice_ProcessGSData( int ent, const uint8_t *data, uint32_t size )
{
	uint32_t    crc_in_packet;
	uint32_t    crc;
	size_t      offset;
	size_t      samples;
	uint16_t    sample_rate;
	uint8_t     vpc_type;
	uint16_t    data_len;
	OpusDecoder *decoder;
	size_t      opus_offset;
	int decoded;
	size_t      silence_samples;
	uint16_t    frame_size;

	if( !data || ent <= 0 || ent > cl.maxclients )
		return 0;

	crc_in_packet = LittleLong( *(uint32_t *)( data + size - sizeof( uint32_t )));
	crc = CRC32_INIT_VALUE;
	CRC32_ProcessBuffer( &crc, data, size - sizeof( uint32_t ));
	crc = CRC32_Final( crc );

	if( crc != crc_in_packet )
	{
		Con_Printf( S_WARN "Voice packet CRC32 mismatch\n" );
		return 0;
	}

	offset = sizeof( uint64_t );
	samples = 0;

	if( offset >= size - sizeof( uint32_t ) || data[offset] != GS_VPC_SETSAMPLERATE )
	{
		Con_Printf( S_WARN "Invalid voice packet type: %d\n", data[offset] );
		return 0;
	}
	offset++;

	if( offset + sizeof( uint32_t ) > size - sizeof( uint32_t ))
		return 0;

	sample_rate = LittleShort( *(uint16_t *)( data + offset ));
	offset += sizeof( uint16_t );

	vpc_type = data[offset++];
	data_len = LittleShort( *(uint16_t *)( data + offset ));
	offset += sizeof( uint16_t );

	if( vpc_type == GS_VPC_VDATA_OPUS_PLC )
	{
		decoder = voice.gs_decoders[ent - 1];
		if( !decoder )
		{
			Con_Printf( S_WARN "No decoder available for entity %d\n", ent );
			return 0;
		}
		opus_offset = 0;
		while( opus_offset + sizeof( uint32_t ) <= data_len )
		{
			frame_size = LittleShort( *(uint16_t *)( data + offset + opus_offset ));
			opus_offset += sizeof( uint32_t );
			// if frame size is 0, it means silence
			if( frame_size == 0 )
			{
				if( samples + VOICE_DEFAULT_SILENCE_FRAME_SIZE > GS_MAX_DECOMPRESSED_SAMPLES )
				{
					Con_Printf( S_WARN "Voice buffer overflow\n" );
					return 0;
				}
				memset( ( (int16_t *)voice.decompress_buffer ) + samples, 0, VOICE_DEFAULT_SILENCE_FRAME_SIZE * sizeof( int16_t ));
				samples += VOICE_DEFAULT_SILENCE_FRAME_SIZE;
				continue;
			}
			if( frame_size == 0xFFFF )
			{
				opus_decoder_ctl( decoder, OPUS_RESET_STATE );
				break;
			}
			if( opus_offset + frame_size > data_len )
			{
				Con_Printf( S_WARN "Opus frame size exceeds data length\n" );
				return 0;
			}
			decoded = opus_decode( decoder, data + offset + opus_offset, frame_size,
					       ( (int16_t *)voice.decompress_buffer ) + samples, voice.frame_size, 0 );
			if( decoded < 0 )
			{
				Con_Printf( S_WARN "Opus decode error: %s\n", opus_strerror( decoded ));
				return 0;
			}
			samples += decoded;
			opus_offset += frame_size;
		}
	}
	else if( vpc_type == GS_VPC_VDATA_SILENCE )
	{
		silence_samples = data_len / 2;
		if( silence_samples > GS_MAX_DECOMPRESSED_SAMPLES )
		{
			Con_Printf( S_WARN "Silence data too large\n" );
			return 0;
		}
		memset( voice.decompress_buffer, 0, silence_samples * sizeof( int16_t ));
		samples = silence_samples;
	}
	else
	{
		Con_Printf( S_WARN "Unsupported voice data type: %d\n", vpc_type );
		return 0;
	}

	return samples;
}

/*
=========================
Voice_CreateGSVoicePacket

Create GoldSrc voice packet
=========================
*/
static uint Voice_CreateGSVoicePacket( byte *out, const byte *voice_data, uint voice_size )
{
	uint     offset = 0;
	uint32_t crc;

	if( !out || !voice_data || voice_size == 0 )
	{
		Con_Printf( S_WARN "Invalid voice data for packet creation\n" );
		return 0;
	}

	if( cls.steamid )
	{
		memcpy( out + offset, cls.steamid, 8 );
	}
	else
	{
		memset( out + offset, 0, 8 ); // fallback: 0
	}

	offset += sizeof( uint64_t );

	out[offset] = GS_VPC_SETSAMPLERATE;
	offset++;

	*(uint16_t *)( out + offset ) = LittleShort( GS_DEFAULT_SAMPLE_RATE );
	offset += sizeof( uint16_t );

	out[offset] = GS_VPC_VDATA_OPUS_PLC;
	offset++;

	*(uint16_t *)( out + offset ) = LittleShort( voice_size );
	offset += sizeof( uint16_t );

	memcpy( out + offset, voice_data, voice_size );
	offset += voice_size;

	crc = CRC32_INIT_VALUE;
	CRC32_ProcessBuffer( &crc, out, offset );
	crc = CRC32_Final( crc );
	*(uint32_t *)( out + offset ) = LittleLong( crc );
	offset += sizeof( uint32_t );

	return offset;
}

/*
===============================================================================

	VOICE CHAT INTEGRATION

===============================================================================
*/

/*
=========================
Voice_Status

Notify user dll about voice transmission
=========================
*/
static void Voice_Status( int entindex, qboolean bTalking )
{
	if( cls.state == ca_active && clgame.dllFuncs.pfnVoiceStatus )
		clgame.dllFuncs.pfnVoiceStatus( entindex, bTalking );
}

/*
=========================
Voice_StatusTimeout

Waits few milliseconds and if there was no
voice transmission, sends notification
=========================
*/
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

/*
=========================
Voice_StatusAck

Sends notification to user dll and
zeroes timeouts for this client
=========================
*/
static void Voice_StatusAck( voice_status_t *status, int playerIndex )
{
	if( !status->talking_ack )
		Voice_Status( playerIndex, true );

	status->talking_ack = true;
	status->talking_timeout = 0.0;
}

/*
=========================
Voice_IsRecording

Check if voice is currently recording
=========================
*/
qboolean Voice_IsRecording( void )
{
	return voice.is_recording;
}

/*
=========================
Voice_RecordStop

Stop voice recording
=========================
*/
void Voice_RecordStop( void )
{
	if( voice.input_file )
	{
		FS_FreeSound( voice.input_file );
		voice.input_file = NULL;
	}

	VoiceCapture_Activate( false );
	voice.is_recording = false;

	Voice_Status( VOICE_LOCALCLIENT_INDEX, false );

	voice.input_buffer_pos = 0;
	memset( voice.input_buffer, 0, sizeof( voice.input_buffer ));
}

/*
=========================
Voice_RecordStart

Start voice recording
=========================
*/
void Voice_RecordStart( void )
{
	Voice_RecordStop();

	if( !voice.initialized )
		return;

	if( voice_inputfromfile.value )
	{
		voice.input_file = FS_LoadSound( "voice_input.wav", NULL, 0 );

		if( voice.input_file )
		{
			Sound_Process( &voice.input_file, voice.samplerate, voice.width, VOICE_PCM_CHANNELS, SOUND_RESAMPLE );
			voice.input_file_pos = 0;

			voice.start_time = Platform_DoubleTime();
			voice.is_recording = true;
		}
		else
		{
			FS_FreeSound( voice.input_file );
			voice.input_file = NULL;
		}
	}

	if( !Voice_IsRecording( ) && voice.device_opened )
		voice.is_recording = VoiceCapture_Activate( true );

	if( Voice_IsRecording())
		Voice_Status( VOICE_LOCALCLIENT_INDEX, true );
}

/*
=========================
Voice_Disconnect

We're disconnected from server
stop recording and notify user dlls
=========================
*/
void Voice_Disconnect( void )
{
	int i;

	Voice_RecordStop();

	if( voice.local.talking_ack )
	{
		Voice_Status( VOICE_LOOPBACK_INDEX, false );
		voice.local.talking_ack = false;
	}

	for( i = 1; i <= MAX_CLIENTS; i++ )
		Voice_Status( i, false );

	VoiceCapture_Shutdown();
	voice.device_opened = false;

	if( voice.goldsrc )
		Voice_ShutdownGoldSrcMode();
	else
		Voice_ShutdownOpusCustomMode();
}

/*
=========================
Voice_StartChannel

Feed the decoded data to engine sound subsystem
=========================
*/
static void Voice_StartChannel( uint samples, byte *data, int entnum )
{
	SND_ForceInitMouth( entnum );
	S_RawEntSamples( entnum, samples, voice.samplerate, voice.width, VOICE_PCM_CHANNELS, data, bound( 0, 255 * voice_scale.value, 255 ));
	Voice_Status( entnum, true );
}

/*
=========================
Voice_StopChannel

Called by mixer when channel idles
=========================
*/
void Voice_StopChannel( int entnum )
{
	Voice_Status( entnum, false );
}


/*
=========================
Voice_AddIncomingData

Received encoded voice data, decode it
=========================
*/
void Voice_LoopbackAck( void )
{
	Voice_StatusAck( &voice.local, VOICE_LOOPBACK_INDEX );
}

/*
=========================
Voice_AddIncomingData

Received encoded voice data, decode it
=========================
*/
void Voice_AddIncomingData( int ent, const byte *data, uint size, uint frames )
{
	const int playernum = ent - 1;
	int samples = 0;
	int ofs = 0;
	voice_status_t *status = NULL;

	if( !voice.initialized || !voice_enable.value )
		return;

	if( voice.goldsrc )
	{
		samples = Voice_ProcessGSData( ent, (const uint8_t *)data, size );
	}
	else
	{
		// Validate player index and decoder
		if( playernum < 0 || playernum >= cl.maxclients || !voice.decoders[playernum] )
			return;

		// decode frame by frame
		for( ;; )
		{
			int      frame_samples;
			uint16_t compressed_size;

			// no compressed size mark
			if( ofs + sizeof( uint16_t ) > size )
				break;

			compressed_size = *(const uint16_t *)( data + ofs );
			ofs += sizeof( uint16_t );

			// no frame data
			if( ofs + compressed_size > size )
				break;

			frame_samples = opus_custom_decode( voice.decoders[playernum], data + ofs, compressed_size,
							    (int16_t *)voice.decompress_buffer + samples, voice.frame_size );

			ofs += compressed_size;
			samples += frame_samples;
		}
	}

	if( samples > 0 )
		Voice_StartChannel( samples, voice.decompress_buffer, ent );
}

/*
=========================
CL_AddVoiceToDatagram

Encode our voice data and send it to server
=========================
*/
void CL_AddVoiceToDatagram( void )
{
	byte buffer[VOICE_MAX_DATA_SIZE];
	uint size, frames;

	if( cls.state != ca_active || !voice.device_opened || !Voice_IsRecording())
		return;

	if( voice.goldsrc )
	{
		if( !voice.gs_encoder )
			return;

		size = Voice_GetGSCompressedData( buffer, sizeof( buffer ), &frames );
		if( size > 0 && MSG_GetNumBytesLeft( &cls.datagram ) >= size + 32 )
		{
			uint packet_size = Voice_CreateGSVoicePacket( voice.compress_buffer, buffer, size );
			MSG_BeginClientCmd( &cls.datagram, clc_voicedata );
			MSG_WriteShort( &cls.datagram, packet_size );
			MSG_WriteBytes( &cls.datagram, voice.compress_buffer, packet_size );
		}
		return;
	}

	if( !voice.encoder )
		return;

	size = Voice_GetOpusCompressedData( voice.compress_buffer, sizeof( voice.compress_buffer ), &frames );
	if( size > 0 && MSG_GetNumBytesLeft( &cls.datagram ) >= size + 32 )
	{
		MSG_BeginClientCmd( &cls.datagram, clc_voicedata );
		MSG_WriteByte( &cls.datagram, voice_loopback.value != 0 );
		MSG_WriteByte( &cls.datagram, frames );
		MSG_WriteShort( &cls.datagram, size );
		MSG_WriteBytes( &cls.datagram, voice.compress_buffer, size );
	}
}

/*
=========================
Voice_RegisterCvars

Register voice related cvars and commands
=========================
*/
void Voice_RegisterCvars( void )
{
	Cvar_RegisterVariable( &voice_enable );
	Cvar_RegisterVariable( &voice_loopback );
	Cvar_RegisterVariable( &voice_scale );
	Cvar_RegisterVariable( &voice_transmit_scale );
	Cvar_RegisterVariable( &voice_avggain );
	Cvar_RegisterVariable( &voice_maxgain );
	Cvar_RegisterVariable( &voice_inputfromfile );
}

/*
=========================
Voice_Shutdown

Completely shutdown the voice subsystem
=========================
*/
static void Voice_Shutdown( void )
{
	Voice_Disconnect();

	voice.initialized = false;
	voice.is_recording = false;
	voice.goldsrc = false;
	voice.start_time = 0.0;
	voice.samplerate = 0;
	voice.frame_size = 0;
	voice.width = 0;

	voice.input_buffer_pos = 0;
	voice.input_file_pos = 0;
	memset( voice.input_buffer, 0, sizeof( voice.input_buffer ));
	memset( voice.compress_buffer, 0, sizeof( voice.compress_buffer ));
	memset( voice.decompress_buffer, 0, sizeof( voice.decompress_buffer ));
	memset( &voice.local, 0, sizeof( voice.local ));
	memset( &voice.autogain, 0, sizeof( voice.autogain ));
}

/*
=========================
Voice_Idle

Run timeout for clients
=========================
*/
void Voice_Idle( double frametime )
{
	int i;

	if( FBitSet( voice_enable.flags, FCVAR_CHANGED ))
	{
		ClearBits( voice_enable.flags, FCVAR_CHANGED );

		if( voice_enable.value )
		{
			if( cls.state == ca_active )
				Voice_Init( voice.codec, voice.quality, false );
		}
		else
			Voice_Shutdown();
	}

	// update local player status first
	Voice_StatusTimeout( &voice.local, VOICE_LOOPBACK_INDEX, frametime );
}

/*
=========================
Voice_Init

Initialize the voice subsystem
=========================
*/
qboolean Voice_Init( const char *pszCodecName, int quality, qboolean preinit )
{
	Q_strncpy( voice.codec, pszCodecName, sizeof( voice.codec ));
	voice.quality = quality;

	if( !voice_enable.value )
		return false;

	if( preinit )
		return true;

	if( Voice_IsGoldSrcMode( pszCodecName ))
	{
		if( !Voice_InitGoldSrcMode( quality ))
		{
			Voice_Shutdown();
			return false;
		}
	}
	else if( Voice_IsOpusCustomMode( pszCodecName ))
	{
		if( !Voice_InitOpusCustomMode( quality ))
		{
			Voice_Shutdown();
			return false;
		}
	}
	else
	{
		// unsupported codec
		Con_Printf( S_WARN "Server requested unsupported voice codec: %s\n", pszCodecName );
		Voice_Shutdown();
		return false;
	}

	voice.device_opened = VoiceCapture_Init();

	if( !voice.device_opened )
		Con_Printf( S_WARN "No microphone is available.\n" );

	voice.initialized = true;
	return true;
}
