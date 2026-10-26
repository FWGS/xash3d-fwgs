import QtQuick 2.0
import Sailfish.Silica 1.0
import Sailfish.Pickers 1.0

Page {
	id: page

	property string updateStatus

	allowedOrientations: Orientation.All

	Connections {
		target: updater
		onCheckFinished: page.updateStatus = status
	}

	SilicaFlickable {
		anchors.fill: parent
		contentHeight: column.height + Theme.paddingLarge

		Column {
			id: column

			width: parent.width

			PageHeader {
				title: qsTr( "Settings" )
			}

			SectionHeader {
				text: qsTr( "Game data" )
			}

			ValueButton {
				label: qsTr( "Folder" )
				value: launcher.baseDir.replace( StandardPaths.home + "/", "" )
				description: qsTr( "Contains game folders, like valve" )
				onClicked: pageStack.push( folderPicker )
			}

			Button {
				anchors.horizontalCenter: parent.horizontalCenter
				visible: launcher.baseDir !== launcher.defaultBaseDir
				text: qsTr( "Use default folder" )
				onClicked: launcher.baseDir = launcher.defaultBaseDir
			}

			SectionHeader {
				visible: updater.enabled
				text: qsTr( "Updates" )
			}

			Button {
				anchors.horizontalCenter: parent.horizontalCenter
				visible: updater.enabled
				enabled: page.updateStatus !== "checking"
				text: qsTr( "Check for updates" )
				onClicked: {
					page.updateStatus = "checking"
					updater.manualCheck()
				}
			}

			Label {
				x: Theme.horizontalPageMargin
				width: parent.width - 2 * Theme.horizontalPageMargin
				visible: text !== ""
				wrapMode: Text.Wrap
				horizontalAlignment: Text.AlignHCenter
				color: Theme.secondaryHighlightColor
				text: {
					switch( page.updateStatus ) {
					case "checking": return qsTr( "Checking for updates…" )
					case "uptodate": return qsTr( "You're on the latest build." )
					case "failed": return qsTr( "Couldn't check for updates. Check your connection and try again." )
					default: return ""
					}
				}
			}

			SectionHeader {
				text: qsTr( "About" )
			}

			Button {
				anchors.horizontalCenter: parent.horizontalCenter
				text: qsTr( "About Xash3D FWGS" )
				onClicked: pageStack.push( Qt.resolvedUrl( "AboutPage.qml" ))
			}
		}

		VerticalScrollDecorator { }
	}

	Component {
		id: folderPicker

		FolderPickerDialog {
			title: qsTr( "Game data folder" )
			path: launcher.baseDir
			onAccepted: launcher.baseDir = selectedPath
		}
	}
}
