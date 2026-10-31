import QtQuick 2.0
import Sailfish.Silica 1.0

ApplicationWindow {
	allowedOrientations: defaultAllowedOrientations

	initialPage: Component {
		Page {
			allowedOrientations: Orientation.All

			Notice {
				id: copied

				text: qsTr( "Copied to clipboard" )
			}

			SilicaFlickable {
				anchors.fill: parent
				contentHeight: column.height + Theme.paddingLarge

				PullDownMenu {
					MenuItem {
						text: qsTr( "Copy to clipboard" )
						onClicked: {
							Clipboard.text = messageTitle + "\n" + messageText
							copied.show()
						}
					}
				}

				Column {
					id: column

					width: parent.width
					spacing: Theme.paddingLarge

					PageHeader {
						title: messageTitle
					}

					Label {
						x: Theme.horizontalPageMargin
						width: parent.width - 2 * Theme.horizontalPageMargin
						text: messageText
						wrapMode: Text.Wrap
						color: Theme.highlightColor
					}

					Button {
						anchors.horizontalCenter: parent.horizontalCenter
						text: qsTr( "OK" )
						onClicked: Qt.quit()
					}
				}

				VerticalScrollDecorator { }
			}
		}
	}
}
