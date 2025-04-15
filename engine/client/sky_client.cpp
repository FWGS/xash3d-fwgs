#include "sky_client.h"
#include "ref_api.h"
#include "gl_export.h"
#include "common.h"
#include "client.h"
#include <sky_client/sky_client.h>
#include <sky_client/profile.h>

void sky::ClientFrame()
{
	static bool Initialized = false;
	if (!Initialized)
	{
		sky::vars::UserClient.emplace();
		Initialized = true;
	}
	auto name = Info_ValueForKey(cls.userinfo, "name");
	if (PROFILE->getName() != name)
	{
		PROFILE->setName(name);
		sky::Log("sky::ClientFrame: new nickname received from userinfo - \"{}\"", name);
	}
	sky::SendNickname();

	if (cls.state == ca_disconnected)
		sky::SetState(sky::State::Disconnected);
	else if (cls.state == ca_connecting || cls.state == ca_connected || cls.state == ca_validate)
		sky::SetState(sky::State::Connecting);
	else
		sky::SetState(sky::State::Game);
}