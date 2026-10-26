import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
	allowedOrientations: Orientation.All

	SilicaListView {
		id: list

		anchors.fill: parent
		model: crashReports.history

		header: PageHeader {
			title: qsTr( "Crash reports" )
		}

		ViewPlaceholder {
			enabled: list.count === 0
			text: qsTr( "No crash reports" )
			hintText: qsTr( "Reports are kept for 30 days and cleared on update" )
		}

		delegate: ListItem {
			id: item

			menu: ContextMenu {
				MenuItem {
					text: qsTr( "Delete" )
					onClicked: item.remorseAction( qsTr( "Deleting crash report" ), function() {
						crashReports.remove( modelData.name )
					})
				}
			}

			Label {
				x: Theme.horizontalPageMargin
				width: parent.width - 2 * Theme.horizontalPageMargin
				anchors.verticalCenter: parent.verticalCenter
				text: Format.formatDate( modelData.date, Formatter.DateMedium ) + ", " + Format.formatDate( modelData.date, Formatter.TimeValue )
				truncationMode: TruncationMode.Fade
				color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
			}

			onClicked: pageStack.push( Qt.resolvedUrl( "CrashPage.qml" ), { name: modelData.name })
		}

		VerticalScrollDecorator { }
	}
}
