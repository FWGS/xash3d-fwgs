import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
	id: page

	property string pendingGame
	property bool startupChecked

	function play( game ) {
		if( launcher.isBundled( game.dir ) || game.downloaded ) {
			launcher.launch( game.dir )
			return
		}

		pendingGame = game.dir
		megabuild.query( game.dir )
	}

	allowedOrientations: Orientation.All

	onStatusChanged: {
		// the countdown is meant for the games list, not for whatever is on top of it
		if( status === PageStatus.Deactivating )
			remorse.cancel()

		if( status !== PageStatus.Active || startupChecked )
			return

		startupChecked = true

		var crash = crashReports.takePending()

		if( crash !== "" ) {
			pageStack.push( Qt.resolvedUrl( "CrashPage.qml" ), { name: crash, fresh: true })
			return
		}

		var game = launcher.lastGame

		if( launcher.startLastGame && game.dir !== undefined ) {
			remorse.execute( qsTr( "Starting %1" ).arg( game.title ), function() {
				page.play( game )
			})
		}
	}

	Connections {
		target: megabuild

		onQueryFinished: {
			if( gamedir !== page.pendingGame )
				return

			page.pendingGame = ""

			if( status === "available" ) {
				remorse.execute( qsTr( "Downloading game libraries" ), function() {
					pageStack.push( Qt.resolvedUrl( "DownloadPage.qml" ), { gamedir: gamedir })
				})
			} else if( status === "unreachable" ) {
				remorse.execute( qsTr( "Library server unreachable, launching anyway" ), function() {
					launcher.launch( gamedir )
				})
			} else {
				launcher.launch( gamedir )
			}
		}

		onInstalledChanged: launcher.refresh()
	}

	Connections {
		target: Qt.application
		onStateChanged: {
			if( Qt.application.state === Qt.ApplicationActive )
				launcher.refresh()
		}
	}

	RemorsePopup { id: remorse }

	BusyIndicator {
		anchors.centerIn: parent
		size: BusyIndicatorSize.Large
		running: page.pendingGame !== ""
	}

	SilicaListView {
		id: list

		anchors.fill: parent
		model: launcher.games

		header: PageHeader {
			title: "Xash3D FWGS"
		}

		PullDownMenu {
			MenuItem {
				text: qsTr( "Settings" )
				onClicked: pageStack.push( Qt.resolvedUrl( "SettingsPage.qml" ))
			}

			MenuItem {
				text: qsTr( "Refresh" )
				onClicked: launcher.refresh()
			}
		}

		ViewPlaceholder {
			enabled: list.count === 0
			text: qsTr( "No games found" )
			hintText: qsTr( "Copy game folders, like valve, to %1" ).arg( launcher.baseDir.replace( StandardPaths.home + "/", "" ))
		}

		delegate: ListItem {
			id: item

			contentHeight: Theme.itemSizeHuge
			enabled: page.pendingGame === ""

			menu: ContextMenu {
				MenuItem {
					text: qsTr( "Game settings" )
					onClicked: pageStack.push( Qt.resolvedUrl( "GamePage.qml" ), { game: modelData })
				}

				MenuItem {
					visible: modelData.downloaded
					text: qsTr( "Remove game libraries" )
					onClicked: item.remorseAction( qsTr( "Removing game libraries" ), function() {
						megabuild.uninstall( modelData.dir )
					})
				}
			}

			Image {
				anchors.fill: parent
				source: "image://cover/" + modelData.path
				sourceSize.width: width
				fillMode: Image.PreserveAspectCrop
				asynchronous: true
				clip: true
				opacity: status === Image.Ready ? 0.4 : 0.0

				Behavior on opacity { FadeAnimator { } }
			}

			Row {
				x: Theme.horizontalPageMargin
				width: parent.width - 2 * Theme.horizontalPageMargin
				anchors.verticalCenter: parent.verticalCenter
				spacing: Theme.paddingLarge

				Image {
					id: icon

					width: Theme.iconSizeMedium
					height: Theme.iconSizeMedium
					sourceSize.width: width
					sourceSize.height: height
					anchors.verticalCenter: parent.verticalCenter
					source: modelData.icon
					visible: modelData.icon !== "" && status !== Image.Error
				}

				Column {
					width: parent.width - ( icon.visible ? icon.width + parent.spacing : 0 )
					anchors.verticalCenter: parent.verticalCenter

					Label {
						width: parent.width
						text: modelData.title
						font.pixelSize: Theme.fontSizeLarge
						truncationMode: TruncationMode.Fade
						color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
					}

					Label {
						width: parent.width
						text: modelData.downloaded ? qsTr( "%1, downloaded libraries" ).arg( modelData.dir ) : modelData.dir
						font.pixelSize: Theme.fontSizeSmall
						truncationMode: TruncationMode.Fade
						color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
					}
				}
			}

			onClicked: page.play( modelData )
		}

		VerticalScrollDecorator { }
	}
}
