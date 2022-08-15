#include "voice.h"

wavdata_t *input_file;
fs_offset_t input_pos;

voice_state_t voice;

CVAR_DEFINE_AUTO( voice_enable, "1", FCVAR_ARCHIVE, "enable voice chat" );
CVAR_DEFINE_AUTO( voice_loopback, "0", 0, "loopback voice back to the speaker" );
CVAR_DEFINE_AUTO( voice_scale, "1.0", FCVAR_ARCHIVE, "incoming voice volume scale" );
CVAR_DEFINE_AUTO( voice_inputfromfile, "0", 0, "input voice from voice_input.wav" );

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

	opus_encoder_ctl( voice.encoder, OPUS_GET_BITRATE( &encoderBitrate ));
	opus_encoder_ctl( voice.encoder, OPUS_GET_COMPLEXITY( &encoderComplexity ));
	opus_encoder_ctl( voice.encoder, OPUS_GET_BANDWIDTH( &encoderBandwidthType ));

	Con_Printf( "Encoder:\n" );
	Con_Printf( "  Bitrate: %.3f kB/second\n", encoderBitrate / 8.0f / 1024.0f );
	Con_Printf( "  Complexity: %d\n", encoderComplexity );
	Con_Printf( "  Bandwidth: " );
	Con_Printf( Voice_GetBandwidthTypeName( encoderBandwidthType ));
	Con_Printf( "\n" );
}

void Voice_RegisterCvars( void )
{
	Cvar_RegisterVariable( &voice_enable );
	Cvar_RegisterVariable( &voice_loopback );
	Cvar_RegisterVariable( &voice_scale );
	Cvar_RegisterVariable( &voice_inputfromfile );
	Cmd_AddClientCommand( "voice_codecinfo", Voice_CodecInfo_f );
}

static void Voice_Status( int entindex, qboolean bTalking )
{
	clgame.dllFuncs.pfnVoiceStatus( entindex, bTalking );
}

// parameters currently unused
qboolean Voice_Init( const char *pszCodecName, int quality )
{
	int err;

	if ( !voice_enable.value )
		return false;

	Voice_DeInit();
	
	voice.was_init = true;

	voice.channels = 1;
	voice.width = 2;
	voice.samplerate = SOUND_48k;
	voice.frame_size = voice.channels * ( (float)voice.samplerate / ( 1000.0f / 20.0f ) ) * voice.width;

	if ( !VoiceCapture_Init() )
	{
		Voice_DeInit();
		return false;
	}

	voice.encoder = opus_encoder_create( voice.samplerate, voice.channels, OPUS_APPLICATION_VOIP, &err );
	voice.decoder = opus_decoder_create( voice.samplerate, voice.channels, &err );

	return true;
}

void Voice_DeInit( void )
{
	if ( !voice.was_init )
		return;

	Voice_RecordStop();

	opus_encoder_destroy( voice.encoder );
	opus_decoder_destroy( voice.decoder );

	voice.was_init = false;
}

uint Voice_GetCompressedData( byte *out, uint maxsize, uint *frames )
{
	uint ofs, size = 0;

	if ( input_file )
	{
		uint numbytes;
		double time;
		
		time = Sys_DoubleTime();

		numbytes = ( time - voice.start_time ) * voice.samplerate;
		numbytes = Q_min( numbytes, input_file->size - input_pos );
		numbytes = Q_min( numbytes, sizeof( voice.buffer ) - voice.buffer_pos );

		memcpy( voice.buffer + voice.buffer_pos, input_file->buffer + input_pos, numbytes );
		voice.buffer_pos += numbytes;
		input_pos += numbytes;

		voice.start_time = time;
	}

	for ( ofs = 0; voice.buffer_pos - ofs >= voice.frame_size && ofs <= voice.buffer_pos; ofs += voice.frame_size )
	{
		int bytes;

		bytes = opus_encode( voice.encoder, (const opus_int16*)(voice.buffer + ofs), voice.frame_size / voice.width, out + size, maxsize );
		memmove( voice.buffer, voice.buffer + voice.frame_size, sizeof( voice.buffer ) - voice.frame_size );
		voice.buffer_pos -= voice.frame_size;

		if ( bytes > 0 )
		{
			size += bytes;
			(*frames)++;
		}
	}

	return size;
}

void Voice_Idle( float frametime )
{
	if ( !voice_enable.value )
	{
		Voice_DeInit();
		return;
	}

	if ( voice.talking_ack )
	{
		voice.talking_timeout += frametime;

		if ( voice.talking_timeout > 0.2f )
		{
			voice.talking_ack = false;
			Voice_Status( -2, false );
		}
	}
}

qboolean Voice_IsRecording( void )
{
	return voice.is_recording;
}

void Voice_RecordStop( void )
{
	if ( input_file )
	{
		FS_FreeSound( input_file );
		input_file = NULL;
	}

	voice.buffer_pos = 0;
	memset( voice.buffer, 0, sizeof( voice.buffer ) );

	if ( Voice_IsRecording() )
		Voice_Status( -1, false );
	
	VoiceCapture_RecordStop();
	
	voice.is_recording = false;
}

void Voice_RecordStart( void )
{
	Voice_RecordStop();

	if ( voice_inputfromfile.value )
	{
		input_file = FS_LoadSound( "voice_input.wav", NULL, 0 );

		if ( input_file )
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

	if ( !Voice_IsRecording() )
		voice.is_recording = VoiceCapture_RecordStart();

	if ( Voice_IsRecording() )
		Voice_Status( -1, true );
}

void Voice_AddIncomingData( int ent, byte *data, uint size, uint frames )
{
	byte decompressed[MAX_RAW_SAMPLES];
	int samples;
	
	samples = opus_decode( voice.decoder, (const byte*)data, size, (short *)decompressed, voice.frame_size / voice.width * frames, false );

	if ( samples > 0 )
		Voice_StartChannel( samples, decompressed, ent );
}

void CL_AddVoiceToDatagram( void )
{
	uint size, frames = 0;
	byte data[MAX_RAW_SAMPLES];

	if ( cls.state != ca_active || !Voice_IsRecording() )
		return;
	
	size = Voice_GetCompressedData( data, sizeof( data ), &frames );

	if ( size > 0 && MSG_GetNumBytesLeft( &cls.datagram ) >= size + 32 )
	{
		MSG_BeginClientCmd( &cls.datagram, clc_voicedata );
		MSG_WriteByte( &cls.datagram, Voice_GetLoopback() );
		MSG_WriteByte( &cls.datagram, frames );
		MSG_WriteShort( &cls.datagram, size );
		MSG_WriteBytes( &cls.datagram, data, size );
	}
}

qboolean Voice_GetLoopback( void )
{
	return voice_loopback.value;
}

void Voice_LocalPlayerTalkingAck( void )
{
	if ( !voice.talking_ack )
	{
		Voice_Status( -2, true );
	}

	voice.talking_ack = true;
	voice.talking_timeout = 0.0f;
}

void Voice_StartChannel( uint samples, byte *data, int entnum )
{
	SND_ForceInitMouth( entnum );
	Voice_Status( entnum, true );
	S_RawEntSamples( entnum, samples, voice.samplerate, voice.width, voice.channels, data, 128.0f * voice_scale.value );
}