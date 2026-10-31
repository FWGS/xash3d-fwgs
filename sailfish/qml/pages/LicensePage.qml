import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
	id: page

	property string title
	property string file
	property string content: launcher.licenseText( file )

	allowedOrientations: Orientation.All

	SilicaFlickable {
		anchors.fill: parent
		contentHeight: column.height + Theme.paddingLarge

		Column {
			id: column

			width: parent.width

			PageHeader {
				title: page.title
			}

			Label {
				x: Theme.horizontalPageMargin
				width: parent.width - 2 * Theme.horizontalPageMargin
				text: page.content !== "" ? page.content : qsTr( "The license text is missing from this build." )
				textFormat: Text.PlainText
				wrapMode: Text.Wrap
				font.pixelSize: Theme.fontSizeExtraSmall
				color: Theme.highlightColor
			}
		}

		VerticalScrollDecorator { }
	}
}
