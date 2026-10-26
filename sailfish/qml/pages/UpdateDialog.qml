import QtQuick 2.0
import Sailfish.Silica 1.0

Dialog {
	property int remoteBuild
	property string url
	property var changelog: []
	property int moreCommits

	allowedOrientations: Orientation.All

	onAccepted: Qt.openUrlExternally( url )
	onRejected: updater.snooze( remoteBuild )

	SilicaFlickable {
		anchors.fill: parent
		contentHeight: column.height + Theme.paddingLarge

		Column {
			id: column

			width: parent.width

			DialogHeader {
				acceptText: qsTr( "Download" )
				cancelText: qsTr( "Later" )
			}

			Label {
				x: Theme.horizontalPageMargin
				width: parent.width - 2 * Theme.horizontalPageMargin
				text: qsTr( "Xash3D FWGS build %1 is available, this is build %2." ).arg( remoteBuild ).arg( updater.buildNumber )
				wrapMode: Text.Wrap
				color: Theme.highlightColor
			}

			SectionHeader {
				visible: changelog.length > 0
				text: qsTr( "Changes since this build" )
			}

			Repeater {
				model: changelog

				Label {
					x: Theme.horizontalPageMargin
					width: column.width - 2 * Theme.horizontalPageMargin
					text: "• " + modelData
					wrapMode: Text.Wrap
					font.pixelSize: Theme.fontSizeSmall
					color: Theme.secondaryHighlightColor
				}
			}

			Label {
				x: Theme.horizontalPageMargin
				width: parent.width - 2 * Theme.horizontalPageMargin
				visible: moreCommits > 0
				text: qsTr( "+ %n more commit(s)", "", moreCommits )
				font.pixelSize: Theme.fontSizeSmall
				color: Theme.secondaryHighlightColor
			}
		}

		VerticalScrollDecorator { }
	}
}
