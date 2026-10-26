/*
settings.h - Sailfish OS launcher settings location
Copyright (C) 2026 Alibek Omarov

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#pragma once

#include <QStandardPaths>
#include <QString>

// sailjail only allows writing into ~/.config/<Org>/<App>/, not the default ~/.config/<Org>/<App>.conf
static inline QString SettingsPath()
{
	return QStandardPaths::writableLocation( QStandardPaths::AppConfigLocation ) + "/launcher.conf";
}
