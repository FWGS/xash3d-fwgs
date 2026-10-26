import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
	allowedOrientations: Orientation.All

	SilicaListView {
		id: list

		anchors.fill: parent
		model: launcher.games

		header: PageHeader {
			title: "Xash3D FWGS"
		}

		PullDownMenu {
			MenuItem {
				text: qsTr( "Refresh" )
				onClicked: launcher.refresh()
			}
		}

		ViewPlaceholder {
			enabled: list.count === 0
			text: qsTr( "No games found" )
			hintText: qsTr( "Copy game folders, like valve, to Documents/xash3d-fwgs" )
		}

		delegate: ListItem {
			contentHeight: Theme.itemSizeMedium

			Column {
				anchors.verticalCenter: parent.verticalCenter
				x: Theme.horizontalPageMargin
				width: parent.width - 2 * Theme.horizontalPageMargin

				Label {
					width: parent.width
					text: modelData.title
					truncationMode: TruncationMode.Fade
					color: highlighted ? Theme.highlightColor : Theme.primaryColor
				}

				Label {
					width: parent.width
					text: modelData.dir
					font.pixelSize: Theme.fontSizeSmall
					truncationMode: TruncationMode.Fade
					color: highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
				}
			}

			onClicked: launcher.launch( modelData.dir )
		}

		VerticalScrollDecorator { }
	}
}
