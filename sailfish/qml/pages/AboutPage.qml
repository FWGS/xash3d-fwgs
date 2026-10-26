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

			ButtonLayout {
				Button {
					text: qsTr( "Source code" )
					onClicked: Qt.openUrlExternally( "https://github.com/FWGS/xash3d-fwgs" )
				}

				Button {
					text: qsTr( "Half-Life SDK license" )
					onClicked: Qt.openUrlExternally( "https://github.com/FWGS/hlsdk-portable/blob/master/LICENSE" )
				}
			}
		}

		VerticalScrollDecorator { }
	}
}
