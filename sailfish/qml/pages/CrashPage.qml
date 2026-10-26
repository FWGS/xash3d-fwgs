import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
	id: page

	property string name
	// shown right after the crash, not opened from the history
	property bool fresh

	readonly property string report: crashReports.report( name )

	allowedOrientations: Orientation.All

	RemorsePopup { id: remorse }

	Notice {
		id: copied

		text: qsTr( "Copied to clipboard" )
	}

	SilicaFlickable {
		anchors.fill: parent
		contentHeight: column.height + Theme.paddingLarge

		PullDownMenu {
			MenuItem {
				text: qsTr( "Delete" )
				onClicked: remorse.execute( qsTr( "Deleting crash log" ), function() {
					crashReports.remove( page.name )
					pageStack.pop()
				})
			}

			MenuItem {
				text: qsTr( "Copy to clipboard" )
				onClicked: {
					Clipboard.text = page.report
					copied.show()
				}
			}

			MenuItem {
				text: qsTr( "Send to developers" )
				onClicked: Qt.openUrlExternally( crashReports.mailUrl( page.name ))
			}
		}

		Column {
			id: column

			width: parent.width
			spacing: Theme.paddingMedium

			PageHeader {
				title: qsTr( "Crash log" )
				description: page.fresh ? qsTr( "Xash3D FWGS crashed last time" ) : ""
			}

			Label {
				x: Theme.horizontalPageMargin
				width: parent.width - 2 * Theme.horizontalPageMargin
				visible: page.fresh
				text: qsTr( "Sending this log to the developers helps fix the problem. Pull down to send or copy it. Crash logs are also kept in Settings." )
				wrapMode: Text.Wrap
				color: Theme.highlightColor
			}

			Label {
				x: Theme.horizontalPageMargin
				width: parent.width - 2 * Theme.horizontalPageMargin
				text: page.report
				wrapMode: Text.WrapAnywhere
				font.family: "monospace"
				font.pixelSize: Theme.fontSizeTiny
				color: Theme.secondaryHighlightColor
			}
		}

		VerticalScrollDecorator { }
	}
}
