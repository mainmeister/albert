// albert extension mpris - a mpris interface plugin for albert
// Copyright (C) 2016-2017 Martin Buergmann
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <QDebug>
#include <QDBusMessage>
#include "main.h"
#include "configwidget.h"
#include "query.h"
#include "xdgiconlookup.h"
#include "command.h"

#define themeOr(name, fallbk)   XdgIconLookup::iconPath(name).isEmpty() ? fallbk : XdgIconLookup::iconPath(name)



/** ***************************************************************************/
class MPRIS::MPRISPrivate {
public:
    ~MPRISPrivate() {
        // If there are still media player objects, delete them
        qDeleteAll(mediaPlayers);
        // Don't need to destruct the command objects.
        // This is done by the destructor of QMap
    }


    const char* name = "MPRIS Control";
    static QDBusMessage findPlayerMsg;
    QPointer<MPRIS::ConfigWidget> widget;
    QList<MPRIS::Player*> mediaPlayers;
    QStringList commands;
    QMap<QString, MPRIS::Command> commandObjects;
};



QDBusMessage MPRIS::MPRISPrivate::findPlayerMsg = QDBusMessage::createMethodCall("org.freedesktop.DBus", "/", "org.freedesktop.DBus", "ListNames");



/** ***************************************************************************/
/** ***************************************************************************/
/** ***************************************************************************/
MPRIS::Extension::Extension()
    : Core::Extension("org.albert.extension.mpris"),
      Core::QueryHandler(Core::Extension::id),
      d(new MPRIS::MPRISPrivate) {
    qDebug("[%s] Initialize extension", d->name);

    QString icon;

    // Setup the DBus commands
    icon = themeOr("media-playback-start", ":play");
    Command* nextToAdd = new Command(
                "play", // Label
                "Start playing", // Title
                "Start playing on %1", // Subtext
                "Play", // DBus Method
                icon
                );
    nextToAdd->applicableWhen("/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player.PlaybackStatus", "Playing", false);
    d->commands.append("play");
    d->commandObjects.insert("play", *nextToAdd);

    icon = themeOr("media-playback-pause", ":pause");
    nextToAdd = new Command(
                "pause",
                "Pause",
                "Pause %1",
                "Pause",
                icon
                );
    nextToAdd->applicableWhen("/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player.PlaybackStatus", "Playing", true);
    d->commands.append("pause");
    d->commandObjects.insert("pause", *nextToAdd);

    icon = themeOr("media-playback-stop", ":stop");
    nextToAdd = new Command(
                "stop",
                "Stop playing",
                "Stop %1",
                "Stop",
                icon
                );
    nextToAdd->applicableWhen("/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player.PlaybackStatus", "Playing", true);
    d->commands.append("stop");
    d->commandObjects.insert("stop", *nextToAdd);

    icon = themeOr("media-skip-forward", ":next");
    nextToAdd = new Command(
                "next",
                "Next track",
                "Play next track on %1",
                "Next",
                icon
                );
    nextToAdd->applicableWhen("/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player.CanGoNext", true, true);
    //.fireCallback([](){qDebug("NEXT");})
    d->commands.append("next");
    d->commandObjects.insert("next", *nextToAdd);

    icon = themeOr("media-skip-backward", ":prev");
    nextToAdd = new Command(
                "previous",
                "Previous track",
                "Play previous track on %1",
                "Previous",
                icon
                );
    nextToAdd->applicableWhen("/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player.CanGoPrevious", true, true);
    d->commands.append("previous");
    d->commandObjects.insert("previous", *nextToAdd);

    qDebug("[%s] Extension initialized", d->name);
}



/** ***************************************************************************/
MPRIS::Extension::~Extension() {

}



/** ***************************************************************************/
QWidget *MPRIS::Extension::widget(QWidget *parent) {
    if (d->widget.isNull()) {
        d->widget = new ConfigWidget(parent);
    }
    return d->widget;
}



/** ***************************************************************************/
void MPRIS::Extension::setupSession() {

    // Clean the memory
    qDeleteAll(d->mediaPlayers);
    d->mediaPlayers.clear();

    // If there is no session bus, abort
    if (!QDBusConnection::sessionBus().isConnected())
        return;

    // Querying the DBus to list all available services
    QDBusMessage response = QDBusConnection::sessionBus().call(MPRISPrivate::findPlayerMsg);

    // Do some error checking
    if (response.type() == QDBusMessage::ReplyMessage) {
        QList<QVariant> args = response.arguments();
        if (args.length() == 1) {
            QVariant arg = args.at(0);
            if (!arg.isNull() && arg.isValid()) {
                QStringList runningBusEndpoints = arg.toStringList();
                if (!runningBusEndpoints.isEmpty()) {
                    // No errors

                    // Filter all mpris capable
                    //names = names.filter(filterRegex);
                    QStringList busids;
                    for (QString& id: runningBusEndpoints) {
                        if (id.startsWith("org.mpris.MediaPlayer2."))
                            busids.append(id);
                    }

                    for (QString& busid : busids) {
                        // And add their player object to the list
                        d->mediaPlayers.append(new Player(busid));
                    }


                } else {
                    qCritical("[%s] DBus error: Argument is either not type of QStringList or is empty!", d->name);
                }
            } else {
                qCritical("[%s] DBus error: Reply argument not valid or null!", d->name);
            }
        } else {
            qCritical("[%s] DBus error: Expected 1 argument for DBus reply. Got %d", d->name, args.length());
        }
    } else {
        qCritical("[%s] DBus error: %s", d->name, response.errorMessage().toStdString().c_str());
    }
}



/** ***************************************************************************/
void MPRIS::Extension::handleQuery(Query *query) {
    // Do not proceed if there are no players running. Why would you even?
    if (d->mediaPlayers.isEmpty())
        return;

    const QString& q = query->searchTerm().toLower();

    // Filter applicable commands
    QStringList cmds;
    for (QString& cmd : d->commands) {
        if (cmd.startsWith(q))
            cmds.append(cmd);
    }


    // For every option create entries for every player
    short percentage = 0;
    for (QString& cmd: cmds) {
        // Calculate how many percent of the query match the command
        percentage = (float)q.length() / (float)cmd.length() *100;

        // Get the command
        Command& toExec = d->commandObjects.find(cmd).value();
        // For every player:
        for (Player* p: d->mediaPlayers) {
            // See if it's applicable for this player
            if (toExec.isApplicable(*p))
                // And add a match if so
                query->addMatch(toExec.produceAlbertItem(*p), percentage);
        }
    }
}



/** ***************************************************************************/
QString MPRIS::Extension::name() const {
    return d->name;
}
