import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
	allowedOrientations: Orientation.All

	SilicaFlickable {
		anchors.fill: parent
		contentHeight: column.height + Theme.paddingLarge

		Column {
			id: column

			width: parent.width
			spacing: Theme.paddingMedium

			PageHeader {
				title: qsTr( "About" )
			}

			Image {
				anchors.horizontalCenter: parent.horizontalCenter
				source: "/usr/share/icons/hicolor/172x172/apps/harbour-xash3d-fwgs.png"
			}

			Label {
				anchors.horizontalCenter: parent.horizontalCenter
				text: "Xash3D FWGS"
				font.pixelSize: Theme.fontSizeExtraLarge
				color: Theme.highlightColor
			}

			Label {
				anchors.horizontalCenter: parent.horizontalCenter
				text: updater.enabled ? qsTr( "Build %1, %2" ).arg( updater.buildNumber ).arg( updater.buildCommit ) : qsTr( "Development build" )
				font.pixelSize: Theme.fontSizeSmall
				color: Theme.secondaryHighlightColor
			}

			Label {
				x: Theme.horizontalPageMargin
				width: parent.width - 2 * Theme.horizontalPageMargin
				text: qsTr( "Xash3D FWGS is a game engine compatible with Half-Life and its mods. It is free software, distributed under the GNU General Public License version 3 or later." )
				wrapMode: Text.Wrap
				color: Theme.highlightColor
			}

			SectionHeader {
				text: qsTr( "Game libraries" )
			}

			Label {
				x: Theme.horizontalPageMargin
				width: parent.width - 2 * Theme.horizontalPageMargin
				text: qsTr( "Game libraries bundled with the app and downloaded from hlsdk-mega-build are built from Half-Life SDK based code and are covered by the Half-Life 1 SDK license. Half-Life is a trademark of Valve Corporation, this app is not affiliated with Valve." )
				wrapMode: Text.Wrap
				color: Theme.highlightColor
			}

			SectionHeader {
				text: qsTr( "App icon" )
			}

			Label {
				x: Theme.horizontalPageMargin
				width: parent.width - 2 * Theme.horizontalPageMargin
				text: qsTr( "The app icon is based on jolla-icon-shapes by Morpog and is distributed under the Creative Commons Attribution-ShareAlike 3.0 Unported license." )
				wrapMode: Text.Wrap
				color: Theme.highlightColor
			}

			ButtonLayout {
				Button {
					text: "jolla-icon-shapes"
					onClicked: Qt.openUrlExternally( "https://github.com/Morpog/jolla-icon-shapes" )
				}

				Button {
					text: qsTr( "CC BY-SA 3.0 license" )
					onClicked: Qt.openUrlExternally( "https://creativecommons.org/licenses/by-sa/3.0/" )
				}
			}

			SectionHeader {
				text: qsTr( "Third-party software" )
			}

			Label {
				x: Theme.horizontalPageMargin
				width: parent.width - 2 * Theme.horizontalPageMargin
				text: qsTr( "Xash3D FWGS includes code from other projects, each under its own license." )
				wrapMode: Text.Wrap
				color: Theme.highlightColor
			}

			Button {
				anchors.horizontalCenter: parent.horizontalCenter
				text: qsTr( "Licenses" )
				onClicked: pageStack.push( Qt.resolvedUrl( "LicensesPage.qml" ))
			}
		}

		VerticalScrollDecorator { }
	}
}
