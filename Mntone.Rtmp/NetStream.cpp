#include "pch.h"
#include "NetStream.h"
#include "NetConnection.h"
#include "RtmpHelper.h"
#include "Media/sound_info.h"
#include "Media/adts_header.h"
#include "Media/video_type.h"
#include "Media/VideoFormat.h"
#include "Media/flv_tag.h"

using namespace Concurrency;
using namespace Windows::Foundation;
using namespace mntone::rtmp;
using namespace mntone::rtmp::media;
using namespace Mntone::Rtmp;
using namespace Mntone::Rtmp::Media;

NetStream::NetStream()
	: streamId_( 0 )
	, audioInfoEnabled_( false )
	, audioInfo_( ref new AudioInfo() )
	, videoInfoEnabled_( false )
	, videoInfo_( ref new VideoInfo() )
	, lengthSizeMinusOne_( 0 )
	, samplingRate_( 0 )
{ }

IAsyncAction^ NetStream::AttachAsync( NetConnection^ connection )
{
	return create_async( [=]
	{
		return connection->AttachNetStreamAsync( this );
	} );
}

void NetStream::AttachedImpl()
{
	Attached( this, ref new NetStreamAttachedEventArgs() );
}

void NetStream::UnattachedImpl()
{
	if( parent_ != nullptr )
	{
		using namespace Mntone::Data::Amf;

		auto cmd = ref new AmfArray();
		cmd->Append( AmfValue::CreateStringValue( "closeStream" ) );	// Command name
		cmd->Append( AmfValue::CreateNumberValue( 0.0 ) );				// Transaction id
		cmd->Append( ref new AmfValue() );								// Command object: set to null type
		cmd->Append( AmfValue::CreateNumberValue( streamId_ ) );
		SendActionAsync( cmd );

		parent_->UnattachNetStream( this );
		parent_ = nullptr;
	}
}

IAsyncAction^ NetStream::PlayAsync( Platform::String^ streamName )
{
	return PlayAsync( streamName, -2 );
}

IAsyncAction^ NetStream::PlayAsync( Platform::String^ streamName, float64 start )
{
	return PlayAsync( streamName, start, -1 );
}

IAsyncAction^ NetStream::PlayAsync( Platform::String^ streamName, float64 start, float64 duration )
{
	return PlayAsync( streamName, start, duration, -1 );
}

IAsyncAction^ NetStream::PlayAsync( Platform::String^ streamName, float64 start, float64 duration, int16 reset )
{
	return create_async( [=]
	{
		using namespace Mntone::Data::Amf;

		auto cmd = ref new AmfArray();
		cmd->Append( AmfValue::CreateStringValue( "play" ) );	// Command name
		cmd->Append( AmfValue::CreateNumberValue( 0.0 ) );		// Transaction id
		cmd->Append( ref new AmfValue() );						// Command object: set to null type
		cmd->Append( AmfValue::CreateStringValue( streamName ) );
		if( start != -2.0 )
		{
			cmd->Append( AmfValue::CreateNumberValue( start ) );
			if( duration != -1.0 )
			{
				cmd->Append( AmfValue::CreateNumberValue( duration ) );
				if( reset != -1 )
				{
					cmd->Append( AmfValue::CreateNumberValue( static_cast<float64>( reset ) ) );
				}
			}
		}
		return SendActionAsync( cmd );
	} );
}

IAsyncAction^ NetStream::PauseAsync( float64 position )
{
	return create_async( [=]
	{
		using namespace Mntone::Data::Amf;

		auto cmd = ref new AmfArray();
		cmd->Append( AmfValue::CreateStringValue( "pause" ) );	// Command name
		cmd->Append( AmfValue::CreateNumberValue( 0.0 ) );		// Transaction id
		cmd->Append( ref new AmfValue() );						// Command object: set to null type
		cmd->Append( AmfValue::CreateBooleanValue( true ) );
		cmd->Append( AmfValue::CreateNumberValue( position ) );
		return SendActionAsync( cmd );
	} );
}

IAsyncAction^ NetStream::ResumeAsync( float64 position ) 
{
	return create_async( [=]
	{
		using namespace Mntone::Data::Amf;

		auto cmd = ref new AmfArray();
		cmd->Append( AmfValue::CreateStringValue( "pause" ) );	// Command name
		cmd->Append( AmfValue::CreateNumberValue( 0.0 ) );		// Transaction id
		cmd->Append( ref new AmfValue() );						// Command object: set to null type
		cmd->Append( AmfValue::CreateBooleanValue( false ) );
		cmd->Append( AmfValue::CreateNumberValue( position ) );
		return SendActionAsync( cmd );
	} );
}

IAsyncAction^ NetStream::SeekAsync( float64 offset )
{
	return create_async( [=]
	{
		using namespace Mntone::Data::Amf;

		auto cmd = ref new AmfArray();
		cmd->Append( AmfValue::CreateStringValue( "seek" ) );	// Command name
		cmd->Append( AmfValue::CreateNumberValue( 0.0 ) );		// Transaction id
		cmd->Append( ref new AmfValue() );						// Command object: set to null type
		cmd->Append( AmfValue::CreateNumberValue( offset ) );
		return SendActionAsync( cmd );
	} );
}

void NetStream::OnMessage( const rtmp_packet packet, std::vector<uint8> data )
{
	switch( packet.type_id_ )
	{
	case type_id_type::audio_message:
		OnAudioMessage( std::move( packet ), std::move( data ) );
		break;

	case type_id_type::video_message:
		OnVideoMessage( std::move( packet ), std::move( data ) );
		break;

	case type_id_type::data_message_amf3:
	case type_id_type::data_message_amf0:
		OnDataMessage( std::move( packet ), std::move( data ) );
		break;

	case type_id_type::command_message_amf3:
	case type_id_type::command_message_amf0:
		OnCommandMessage( std::move( packet ), std::move( data ) );
		break;

	case type_id_type::aggregate_message:
		OnAggregateMessage( std::move( packet ), std::move( data ) );
		break;
	}
}

void NetStream::OnAudioMessage( const rtmp_packet packet, std::vector<uint8> data )
{
	const auto& si = *reinterpret_cast<sound_info*>( data.data() );

	if( si.format == sound_format::aac )
	{
		if( data[1] == 0x01 )
		{
			auto args = ref new NetStreamAudioReceivedEventArgs();
			args->Info = audioInfo_;
			args->SetTimestamp( packet.timestamp_ );
			args->SetData( std::move( data ), 2 );
			AudioReceived( this, args );
		}
		else if( data[1] == 0x00 )
		{
			const auto& adts = *reinterpret_cast<adts_header*>( data.data() );
			audioInfo_->Format = AudioFormat::Aac;
			audioInfo_->SampleRate = samplingRate_ != 0 ? samplingRate_ : adts.sampling_frequency();
			audioInfo_->ChannelCount = adts.channel_configuration();
			audioInfo_->BitsPerSample = si.size == sound_size::s16bit ? 16 : 8;
			AudioStarted( this, ref new NetStreamAudioStartedEventArgs( audioInfo_ ) );
		}
		return;
	}

	if( !audioInfoEnabled_ )
	{
		audioInfo_->SetInfo( si );
		audioInfoEnabled_ = true;
		AudioStarted( this, ref new NetStreamAudioStartedEventArgs( audioInfo_ ) );
	}

	auto args = ref new NetStreamAudioReceivedEventArgs();
	args->Info = audioInfo_;
	args->SetTimestamp( packet.timestamp_ );
	args->SetData( std::move( data ), 1 );
	AudioReceived( this, args );
}

void NetStream::OnVideoMessage( const rtmp_packet packet, std::vector<uint8> data )
{
	const auto& vt = static_cast<video_type>( ( data[0] >> 4 ) & 0x0f );
	const auto& vf = static_cast<VideoFormat>( data[0] & 0x0f );

	auto args = ref new NetStreamVideoReceivedEventArgs();
	args->IsKeyframe = vt == video_type::keyframe;
	args->SetDecodeTimestamp( packet.timestamp_ );

	if( vf == VideoFormat::Avc )
	{
		// Need to convert NAL file stream to byte stream
		AnalysisAvc( std::move( packet ), std::move( data ), args );
		return;
	}

	if( !videoInfoEnabled_ )
	{
		videoInfo_->Format = vf;
		videoInfoEnabled_ = true;
		VideoStarted( this, ref new NetStreamVideoStartedEventArgs( videoInfo_ ) );
	}

	args->Info = videoInfo_;
	args->SetPresentationTimestamp( packet.timestamp_ );
	args->SetData( std::move( data ), 1 );
	VideoReceived( this, args );
}

void NetStream::OnDataMessage( const rtmp_packet /*packet*/, std::vector<uint8> data )
{
	const auto& amf = RtmpHelper::ParseAmf( std::move( data ) );
	const auto& name = amf->GetStringAt( 0 );
	if( name != "onMetaData" )
	{
		return;
	}

	const auto& object = amf->GetObjectAt( 1 );
	if( object->HasKey( "audiosamplerate" ) )
	{
		samplingRate_ = static_cast<uint32>( object->GetNamedNumber( "audiosamplerate" ) );
	}
}

void NetStream::OnCommandMessage( const rtmp_packet /*packet*/, std::vector<uint8> data )
{
	const auto& amf = RtmpHelper::ParseAmf( std::move( data ) );
	const auto& name = amf->GetStringAt( 0 );
	if( name != "onStatus" )
	{
		return;
	}

	const auto& information = amf->GetObjectAt( 3 );
	const auto& code = information->GetNamedString( "code" );
	const auto& nsc = RtmpHelper::ParseNetStreamCode( code->Data() );
	StatusUpdated( this, ref new NetStatusUpdatedEventArgs( nsc ) );
}

void NetStream::OnAggregateMessage( const rtmp_packet packet, std::vector<uint8> data )
{
	if( data.size() < 11 )
	{
		return;
	}

	auto itr = data.cbegin();
	do
	{
		const auto& tag = *reinterpret_cast<const flv_tag*>( &itr[0] );
		itr += 11;

		auto clone_packet = packet;
		clone_packet.timestamp_ = tag.timestamp();
		clone_packet.type_id_ = static_cast<type_id_type>( tag.tag_type() );

		auto end_of_sequence = itr + tag.data_size();

		std::vector<uint8> subset_data( itr, end_of_sequence );
		switch( tag.tag_type() )
		{
		case flv_tag_type::audio:
			OnAudioMessage( std::move( clone_packet ), std::move( subset_data ) );
			break;

		case flv_tag_type::video:
			OnVideoMessage( std::move( clone_packet ), std::move( subset_data ) );
			break;

		case flv_tag_type::script_data:
			OnDataMessage( std::move( clone_packet ), std::move( subset_data ) );
			break;
		}
		itr = end_of_sequence + 4;
	} while( itr < data.cend() );
}

task<void> NetStream::SendActionAsync( Mntone::Data::Amf::AmfArray^ amf )
{
	if( parent_ != nullptr )
	{
		return parent_->SendActionAsync( streamId_, amf );
	}

	return create_task( [] { } );
}