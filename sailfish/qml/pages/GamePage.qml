import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
	id: page

	property var game
	property var info: megabuild.installedInfo( game.dir )
	property var source: info.source ? info.source : ({})
	property bool downloaded: info.filename !== undefined
	property bool updateAvailable

	allowedOrientations: Orientation.All

	Component.onCompleted: {
		if( downloaded )
			megabuild.query( game.dir )
	}

	Connections {
		target: megabuild

		onQueryFinished: {
			if( gamedir === page.game.dir )
				page.updateAvailable = megabuild.updateAvailable( gamedir )
		}

		onInstalledChanged: {
			if( gamedir !== page.game.dir )
				return

			page.info = megabuild.installedInfo( gamedir )
			page.updateAvailable = false
		}
	}

	RemorsePopup { id: remorse }

	SilicaFlickable {
		anchors.fill: parent
		contentHeight: column.height + Theme.paddingLarge

		Column {
			id: column

			width: parent.width

			PageHeader {
				title: page.game.title
				description: page.game.dir
			}

			Image {
				width: parent.width
				height: status === Image.Ready ? width * 9 / 16 : 0
				source: "image://cover/" + page.game.path
				sourceSize.width: width
				fillMode: Image.PreserveAspectCrop
				asynchronous: true
				clip: true
			}

			SectionHeader {
				text: qsTr( "Launch options" )
			}

			TextField {
				width: parent.width
				label: qsTr( "Command-line arguments" )
				placeholderText: launcher.defaultArguments
				text: launcher.gameArguments( page.game.dir )
				inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
				onTextChanged: {
					if( text !== launcher.gameArguments( page.game.dir ))
						launcher.setGameArguments( page.game.dir, text )
				}

				EnterKey.iconSource: "image://theme/icon-m-enter-close"
				EnterKey.onClicked: focus = false
			}

			Label {
				x: Theme.horizontalPageMargin
				width: parent.width - 2 * Theme.horizontalPageMargin
				text: qsTr( "-console enables the developer console, -log writes engine.log into the game data folder" )
				font.pixelSize: Theme.fontSizeExtraSmall
				wrapMode: Text.Wrap
				color: Theme.secondaryHighlightColor
			}

			SectionHeader {
				visible: page.downloaded
				text: qsTr( "Game libraries" )
			}

			DetailItem {
				visible: page.downloaded
				label: qsTr( "Platform" )
				value: page.info.platform || "—"
			}

			DetailItem {
				visible: page.downloaded
				label: qsTr( "Branch" )
				value: page.source.branch || "—"
			}

			DetailItem {
				visible: page.downloaded
				label: qsTr( "Commit" )
				value: page.source.commit ? page.source.commit.substr( 0, 12 ) : "—"
			}

			DetailItem {
				visible: page.downloaded
				label: qsTr( "Downloaded" )
				value: page.info.downloaded ? Format.formatDate( page.info.downloaded, Formatter.DateMedium ) : "—"
			}

			ButtonLayout {
				visible: page.downloaded

				Button {
					visible: page.source.url !== undefined
					text: qsTr( "Source code" )
					onClicked: Qt.openUrlExternally( page.source.url )
				}

				Button {
					visible: page.updateAvailable
					text: qsTr( "Update" )
					onClicked: pageStack.push( Qt.resolvedUrl( "DownloadPage.qml" ), { gamedir: page.game.dir })
				}

				Button {
					text: qsTr( "Remove" )
					onClicked: remorse.execute( qsTr( "Removing game libraries" ), function() {
						megabuild.uninstall( page.game.dir )
					})
				}
			}
		}

		VerticalScrollDecorator { }
	}
}
