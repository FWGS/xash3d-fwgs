import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
	allowedOrientations: Orientation.All

	// file is a text shipped in the package, url is opened for the rest
	ListModel {
		id: licenses

		ListElement { name: "Xash3D FWGS"; license: "GNU GPL 3.0 or later"; file: ""; url: "https://www.gnu.org/licenses/gpl-3.0.html" }
		ListElement { name: "hlsdk-portable"; license: "Half-Life 1 SDK License"; file: "hlsdk-portable.txt"; url: "" }
		ListElement { name: "mainui"; license: "GNU GPL 2.0 or later"; file: ""; url: "https://www.gnu.org/licenses/old-licenses/gpl-2.0.html" }
		ListElement { name: "MiniUTL"; license: "BSD 3-Clause"; file: "miniutl.txt"; url: "" }
		ListElement { name: "nanoGL"; license: "GNU GPL 2.0 or later"; file: ""; url: "https://www.gnu.org/licenses/old-licenses/gpl-2.0.html" }
		ListElement { name: "gl4es"; license: "MIT"; file: "gl4es.txt"; url: "" }
		ListElement { name: "Opus"; license: "BSD 3-Clause"; file: "opus.txt"; url: "" }
		ListElement { name: "Opusfile"; license: "BSD 3-Clause"; file: "opusfile.txt"; url: "" }
		ListElement { name: "mpg123"; license: "GNU LGPL 2.1"; file: "mpg123.txt"; url: "" }
		ListElement { name: "Mbed TLS"; license: "Apache 2.0 or GNU GPL 2.0 or later"; file: "mbedtls.txt"; url: "" }
		ListElement { name: "libbacktrace"; license: "BSD 3-Clause"; file: "libbacktrace.txt"; url: "" }
		ListElement { name: "MultiEmulator"; license: "MIT"; file: "multiemulator.txt"; url: "" }
		ListElement { name: "miniz"; license: "MIT"; file: ""; url: "https://github.com/richgel999/miniz/blob/master/LICENSE" }
		ListElement { name: "stb_truetype"; license: "Public domain or MIT"; file: ""; url: "https://github.com/nothings/stb" }
		ListElement { name: "SipHash"; license: "CC0 1.0"; file: ""; url: "https://github.com/veorq/SipHash" }
		ListElement { name: "SDL_GameControllerDB"; license: "zlib"; file: ""; url: "https://github.com/mdqinc/SDL_GameControllerDB" }
		ListElement { name: "Mozilla CA certificates"; license: "MPL 2.0"; file: ""; url: "https://www.mozilla.org/MPL/2.0/" }
		ListElement { name: "jolla-icon-shapes"; license: "CC BY-SA 3.0"; file: ""; url: "https://creativecommons.org/licenses/by-sa/3.0/" }
	}

	SilicaListView {
		anchors.fill: parent
		model: licenses

		header: PageHeader {
			title: qsTr( "Licenses" )
		}

		delegate: ListItem {
			id: item

			contentHeight: Theme.itemSizeMedium

			Column {
				x: Theme.horizontalPageMargin
				width: parent.width - 2 * Theme.horizontalPageMargin
				anchors.verticalCenter: parent.verticalCenter

				Label {
					width: parent.width
					text: model.name
					truncationMode: TruncationMode.Fade
					color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
				}

				Label {
					width: parent.width
					text: model.license
					font.pixelSize: Theme.fontSizeSmall
					truncationMode: TruncationMode.Fade
					color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
				}
			}

			onClicked: {
				if( model.file !== "" )
					pageStack.push( Qt.resolvedUrl( "LicensePage.qml" ), { title: model.name, file: model.file })
				else
					Qt.openUrlExternally( model.url )
			}
		}

		VerticalScrollDecorator { }
	}
}
