#include "pch.h"
#include "NetStreamAudioStartedEventArgs.h"

using namespace Mntone::Rtmp;

NetStreamAudioStartedEventArgs::NetStreamAudioStartedEventArgs( AudioInfo^ info ):
	_Info( info )
{ }