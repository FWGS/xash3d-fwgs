import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
	id: page

	property string gamedir
	property string error
	property bool failed

	allowedOrientations: Orientation.All
	backNavigation: failed

	Component.onCompleted: megabuild.install( gamedir )

	Connections {
		target: megabuild

		onProgress: {
			bar.indeterminate = total <= 0

			if( total > 0 ) {
				bar.maximumValue = total
				bar.value = received
				bar.valueText = Format.formatFileSize( received ) + " / " + Format.formatFileSize( total )
			} else {
				bar.valueText = received > 0 ? Format.formatFileSize( received ) : ""
			}
		}

		onInstallFinished: {
			if( gamedir !== page.gamedir )
				return

			if( success ) {
				launcher.launch( gamedir )
			} else if( error === "" ) {
				// cancelled
				pageStack.pop()
			} else {
				page.error = error
				page.failed = true
			}
		}
	}

	PageHeader {
		id: header
		title: page.failed ? qsTr( "Download failed" ) : qsTr( "Downloading game libraries" )
	}

	Column {
		width: parent.width
		anchors.verticalCenter: parent.verticalCenter
		spacing: Theme.paddingLarge

		ProgressBar {
			id: bar

			width: parent.width
			visible: !page.failed
			indeterminate: true
			label: page.gamedir
		}

		Label {
			x: Theme.horizontalPageMargin
			width: parent.width - 2 * Theme.horizontalPageMargin
			visible: page.failed
			text: page.error
			wrapMode: Text.Wrap
			horizontalAlignment: Text.AlignHCenter
			color: Theme.secondaryHighlightColor
		}

		Button {
			anchors.horizontalCenter: parent.horizontalCenter
			text: page.failed ? qsTr( "Close" ) : qsTr( "Cancel" )
			onClicked: {
				if( page.failed )
					pageStack.pop()
				else
					megabuild.cancel()
			}
		}
	}
}
