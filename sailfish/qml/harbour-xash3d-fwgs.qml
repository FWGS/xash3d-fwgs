import QtQuick 2.0
import Sailfish.Silica 1.0
import "pages"

ApplicationWindow {
	initialPage: Component { GamesPage { } }
	cover: Qt.resolvedUrl( "cover/CoverPage.qml" )
	allowedOrientations: defaultAllowedOrientations

	Connections {
		target: updater
		onUpdateAvailable: pageStack.push( Qt.resolvedUrl( "pages/UpdateDialog.qml" ), {
			remoteBuild: remoteBuild,
			url: url,
			changelog: changelog,
			moreCommits: moreCommits
		})
	}

	Component.onCompleted: updater.autoCheck()
}
