/***************************************************************************
 *   Copyright (C) 2003 by S�astien Laot                                 *
 *   slaout@linux62.org                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include <q3dragobject.h>
#include <qdir.h>
#include <qpainter.h>
#include <qtextcodec.h>
#include <qbuffer.h>
//Added by qt3to4:
#include <Q3TextStream>
#include <Q3CString>
#include <Q3ValueList>
#include <QPixmap>
#include <k3urldrag.h>
#include <kdeversion.h>
#include <kapplication.h>
#include <qdesktopwidget.h>

#include "basket.h"
#include "notedrag.h"
#include "notefactory.h"
#include "tools.h"
#include "global.h"

#include <KIO/CopyJob>

/** NoteDrag */

const char * NoteDrag::NOTE_MIME_STRING = "application/x-basket-note";

void NoteDrag::createAndEmptyCuttingTmpFolder()
{
	Tools::deleteRecursively(Global::tempCutFolder());
	QDir dir;
	dir.mkdir(Global::tempCutFolder());
}

Q3DragObject* NoteDrag::dragObject(NoteSelection *noteList, bool cutting, QWidget *source)
{
	if (noteList->count() <= 0)
		return 0;

	// The MimeSource:
	K3MultipleDrag *multipleDrag = new K3MultipleDrag(source);

	// Make sure the temporary folder exists and is empty (we delete previously moved file(s) (if exists)
	// since we override the content of the clipboard and previous file willn't be accessable anymore):
	createAndEmptyCuttingTmpFolder();

	// The "Native Format" Serialization:
	QBuffer buffer;
	if (buffer.open(QIODevice::WriteOnly)) {
		QDataStream stream(&buffer);
		// First append a pointer to the basket:
		stream << (quint64)(noteList->firstStacked()->note->basket());
		// Then a list of pointers to all notes, and parent groups:
		for (NoteSelection *node = noteList->firstStacked(); node; node = node->nextStacked())
			stream << (quint64)(node->note);
		Q3ValueList<Note*> groups = noteList->parentGroups();
		for (Q3ValueList<Note*>::iterator it = groups.begin(); it != groups.end(); ++it)
			stream << (quint64)(*it);
		stream << (quint64)0;
		// And finally the notes themselves:
		serializeNotes(noteList, stream, cutting);
		// Append the object:
		buffer.close();
		Q3StoredDrag *dragObject = new Q3StoredDrag(NOTE_MIME_STRING, source);
		dragObject->setEncodedData(buffer.buffer());
		multipleDrag->addDragObject(dragObject);
	}

	// The "Other Flavours" Serialization:
	serializeText(  noteList, multipleDrag          );
	serializeHtml(  noteList, multipleDrag          );
	serializeImage( noteList, multipleDrag          );
	serializeLinks( noteList, multipleDrag, cutting );

	// The Alternate Flavours:
	if (noteList->count() == 1)
		noteList->firstStacked()->note->content()->addAlternateDragObjects(multipleDrag);

	// If it is a drag, and not a copy/cut, add the feedback pixmap:
	if (source)
		setFeedbackPixmap(noteList, multipleDrag);

	return multipleDrag;
}

void NoteDrag::serializeNotes(NoteSelection *noteList, QDataStream &stream, bool cutting)
{
	for (NoteSelection *node = noteList; node; node = node->next) {
		stream << (quint64)(node->note);
		if (node->firstChild) {
			stream << (quint64)(NoteType::Group) << (quint64)(node->note->groupWidth()) << (quint64)(node->note->isFolded());
			serializeNotes(node->firstChild, stream, cutting);
		} else {
			NoteContent *content = node->note->content();
			stream << (quint64)(content->type()) << (quint64)(node->note->groupWidth());
			// Serialize file name, and move the file to a temporary place if the note is to be cuttted.
			// If note does not have file name, we append empty string to be able to easily decode the notes later:
			stream << content->fileName();
			if (content->shouldSerializeFile()) {
				if (cutting) {
					// Move file in a temporary place:
					QString fullPath = Global::tempCutFolder() + Tools::fileNameForNewFile(content->fileName(), Global::tempCutFolder());
					KIO::move(KUrl(content->fullPath()), KUrl(fullPath), /*showProgressInfo=*/false);
					node->fullPath = fullPath;
					stream << fullPath;
				} else
					stream << content->fullPath();
			} else
				stream << QString("");
			stream << content->note()->addedDate() << content->note()->lastModificationDate();
			content->serialize(stream);
			State::List states = node->note->states();
			for (State::List::Iterator it = states.begin(); it != states.end(); ++it)
				stream << (quint64)(*it);
			stream << (quint64)0;
		}
	}
	stream << (quint64)0; // Mark the end of the notes in this group/hierarchy.
}

void NoteDrag::serializeText(NoteSelection *noteList, K3MultipleDrag *multipleDrag)
{
	QString textEquivalent;
	QString text;
	for (NoteSelection *node = noteList->firstStacked(); node; node = node->nextStacked()) {
		text = node->note->toText(node->fullPath); // note->toText() and not note->content()->toText() because the first one will also export the tags as text.
		if (!text.isEmpty())
			textEquivalent += (!textEquivalent.isEmpty() ? "\n" : "") + text;
	}
	if (!textEquivalent.isEmpty())
		multipleDrag->addDragObject( new Q3TextDrag(textEquivalent) );
}

void NoteDrag::serializeHtml(NoteSelection *noteList, K3MultipleDrag *multipleDrag)
{
	QString htmlEquivalent;
	QString html;
	for (NoteSelection *node = noteList->firstStacked(); node; node = node->nextStacked()) {
		html = node->note->content()->toHtml("", node->fullPath);
		if (!html.isEmpty())
			htmlEquivalent += (!htmlEquivalent.isEmpty() ? "<br>\n" : "") + html;
	}
	if (!htmlEquivalent.isEmpty()) {
		// Add HTML flavour:
		Q3TextDrag *htmlDrag = new Q3TextDrag(htmlEquivalent);
		htmlDrag->setSubtype("html");
		multipleDrag->addDragObject(htmlDrag);
		// But also QTextEdit flavour, to be able to paste several notes to a text edit:
		QByteArray byteArray = ("<!--StartFragment--><p>" + htmlEquivalent).local8Bit();
		Q3StoredDrag *richTextDrag = new Q3StoredDrag("application/x-qrichtext");
		richTextDrag->setEncodedData(byteArray);
		multipleDrag->addDragObject(richTextDrag);
	}
}

void NoteDrag::serializeImage(NoteSelection *noteList, K3MultipleDrag *multipleDrag)
{
	Q3ValueList<QPixmap> pixmaps;
	QPixmap pixmap;
	for (NoteSelection *node = noteList->firstStacked(); node; node = node->nextStacked()) {
		pixmap = node->note->content()->toPixmap();
		if (!pixmap.isNull())
			pixmaps.append(pixmap);
	}
	if (!pixmaps.isEmpty()) {
		QPixmap pixmapEquivalent;
		if (pixmaps.count() == 1)
			pixmapEquivalent = pixmaps[0];
		else {
			// Search the total size:
			int height = 0;
			int width  = 0;
			for (Q3ValueList<QPixmap>::iterator it = pixmaps.begin(); it != pixmaps.end(); ++it) {
				height += (*it).height();
				if ((*it).width() > width)
					width = (*it).width();
			}
			// Create the image by painting all image into one big image:
			pixmapEquivalent.resize(width, height);
			pixmapEquivalent.fill(Qt::white);
			QPainter painter(&pixmapEquivalent);
			height = 0;
			for (Q3ValueList<QPixmap>::iterator it = pixmaps.begin(); it != pixmaps.end(); ++it) {
				painter.drawPixmap(0, height, *it);
				height += (*it).height();
			}
		}
		Q3ImageDrag *imageDrag = new Q3ImageDrag(pixmapEquivalent.convertToImage());
		multipleDrag->addDragObject(imageDrag);
	}
}

void NoteDrag::serializeLinks(NoteSelection *noteList, K3MultipleDrag *multipleDrag, bool cutting)
{
	KUrl::List  urls;
	QStringList titles;
	KUrl    url;
	QString title;
	for (NoteSelection *node = noteList->firstStacked(); node; node = node->nextStacked()) {
		node->note->content()->toLink(&url, &title, node->fullPath);
		if (!url.isEmpty()) {
			urls.append(url);
			titles.append(title);
		}
	}
	if (!urls.isEmpty()) {
		// First, the standard text/uri-list MIME format:
#if KDE_IS_VERSION( 3, 3, 90 )
		K3URLDrag *urlsDrag = new K3URLDrag(urls);
		// ONLY export as text/uri-list, and not as text/plain* as we wil do that better ourself
		urlsDrag->setExportAsText(false);
		multipleDrag->addDragObject(urlsDrag);
#else
		K3URLDrag2 *urlsDrag = new K3URLDrag2(urls);
		QByteArray byteArray = urlsDrag->encodedData2("text/uri-list");
		Q3StoredDrag *uriListDrag = new Q3StoredDrag("text/uri-list");
		uriListDrag->setEncodedData(byteArray);
		multipleDrag->addDragObject(uriListDrag);
		delete urlsDrag;
#endif
		// Then, also provide it in the Mozilla proprietary format (that also allow to add titles to URLs):
		// A version for Mozilla applications (convert to "theUrl\ntheTitle", into UTF-16):
		// FIXME: Does Mozilla support the drag of several URLs at once?
		// FIXME: If no, only provide that if theire is only ONE URL.
		QString xMozUrl;
		for (uint i = 0; i < urls.count(); ++i)
			xMozUrl += (xMozUrl.isEmpty() ? "" : "\n") + urls[i].prettyUrl() + "\n" + titles[i];
/*		Code for only one: ===============
		xMozUrl = note->title() + "\n" + note->url().prettyUrl();*/
		QByteArray baMozUrl;
		Q3TextStream stream(baMozUrl, QIODevice::WriteOnly);
		stream.setEncoding(Q3TextStream::RawUnicode); // It's UTF16 (aka UCS2), but with the first two order bytes
		stream << xMozUrl;
		Q3StoredDrag *xMozUrlDrag = new Q3StoredDrag("text/x-moz-url");
		xMozUrlDrag->setEncodedData(baMozUrl);
		multipleDrag->addDragObject(xMozUrlDrag);

		if (cutting) {
			QByteArray  arrayCut(2);
			Q3StoredDrag *storedDragCut = new Q3StoredDrag("application/x-kde-cutselection");
			arrayCut[0] = '1';
			arrayCut[1] = 0;
			storedDragCut->setEncodedData(arrayCut);
			multipleDrag->addDragObject(storedDragCut);
		}
	}
}

void NoteDrag::setFeedbackPixmap(NoteSelection *noteList, K3MultipleDrag *multipleDrag)
{
	QPixmap pixmap = feedbackPixmap(noteList);
	if (!pixmap.isNull())
		multipleDrag->setPixmap(pixmap, QPoint(-8, -8));
}

QPixmap NoteDrag::feedbackPixmap(NoteSelection *noteList)
{
	if (noteList == 0)
		return QPixmap();

	static const int MARGIN  = 2;
	static const int SPACING = 1;

	QColor textColor       = noteList->firstStacked()->note->basket()->textColor();
	QColor backgroundColor = noteList->firstStacked()->note->basket()->backgroundColor().dark(NoteContent::FEEDBACK_DARKING);

	Q3ValueList<QPixmap> pixmaps;
	Q3ValueList<QColor>  backgrounds;
	Q3ValueList<bool>    spaces;
	QPixmap pixmap;
	int height = 0;
	int width  = 0;
	int i      = 0;
	bool elipsisImage = false;
	QColor bgColor;
	bool needSpace;
	for (NoteSelection *node = noteList->firstStacked(); node; node = node->nextStacked(), ++i) {
		if (elipsisImage) {
			pixmap = QPixmap(7, 2);
			pixmap.fill(backgroundColor);
			QPainter painter(&pixmap);
			painter.setPen(textColor);
			painter.drawPoint(1, 1);
			painter.drawPoint(3, 1);
			painter.drawPoint(5, 1);
			painter.end();
			bgColor   = node->note->basket()->backgroundColor();
			needSpace = false;
		} else {
			pixmap    = node->note->content()->feedbackPixmap(/*maxWidth=*/kapp->desktop()->width() / 2, /*maxHeight=*/96);
			bgColor   = node->note->backgroundColor();
			needSpace = node->note->content()->needSpaceForFeedbackPixmap();
		}
		if (!pixmap.isNull()) {
			if (pixmap.width() > width)
				width = pixmap.width();
			pixmaps.append(pixmap);
			backgrounds.append(bgColor);
			spaces.append(needSpace);
			height += (i > 0 && needSpace ? 1 : 0) + pixmap.height() + SPACING + (needSpace ? 1 : 0);
			if (elipsisImage)
				break;
			if (height > kapp->desktop()->height() / 2)
				elipsisImage = true;
		}
	}
	if (!pixmaps.isEmpty()) {
		QPixmap result(MARGIN + width + MARGIN, MARGIN + height - SPACING + MARGIN - (spaces.last() ? 1 : 0));
		QPainter painter(&result);
		// Draw all the images:
		height = MARGIN;
		Q3ValueList<QPixmap>::iterator it;
		Q3ValueList<QColor>::iterator  it2;
		Q3ValueList<bool>::iterator    it3;
		int i = 0;
		for (it = pixmaps.begin(), it2 = backgrounds.begin(), it3 = spaces.begin(); it != pixmaps.end(); ++it, ++it2, ++it3, ++i) {
			if (i != 0 && (*it3)) {
				painter.fillRect(MARGIN, height, result.width() - 2 * MARGIN, SPACING, (*it2).dark(NoteContent::FEEDBACK_DARKING));
				++height;
			}
			painter.drawPixmap(MARGIN, height, *it);
			if ((*it).width() < width)
				painter.fillRect(MARGIN + (*it).width(), height, width - (*it).width(), (*it).height(), (*it2).dark(NoteContent::FEEDBACK_DARKING));
			if (*it3) {
				painter.fillRect(MARGIN, height + (*it).height(), result.width() - 2 * MARGIN, SPACING, (*it2).dark(NoteContent::FEEDBACK_DARKING));
				++height;
			}
			painter.fillRect(MARGIN, height + (*it).height(), result.width() - 2 * MARGIN, SPACING, Tools::mixColor(textColor, backgroundColor));
			height += (*it).height() + SPACING;
		}
		// Draw the border:
		painter.setPen(textColor);
		painter.drawLine(0,                  0,                   result.width() - 1, 0);
		painter.drawLine(0,                  0,                   0,                  result.height() - 1);
		painter.drawLine(0,                  result.height() - 1, result.width() - 1, result.height() - 1);
		painter.drawLine(result.width() - 1, 0,                   result.width() - 1, result.height() - 1);
		// Draw the "lightly rounded" border:
		painter.setPen(Tools::mixColor(textColor, backgroundColor));
		painter.drawPoint(0,                  0);
		painter.drawPoint(0,                  result.height() - 1);
		painter.drawPoint(result.width() - 1, result.height() - 1);
		painter.drawPoint(result.width() - 1, 0);
		// Draw the background in the margin (the inside will be painted above, anyway):
		painter.setPen(backgroundColor);
		painter.drawLine(1,                  1,                   result.width() - 2, 1);
		painter.drawLine(1,                  1,                   1,                  result.height() - 2);
		painter.drawLine(1,                  result.height() - 2, result.width() - 2, result.height() - 2);
		painter.drawLine(result.width() - 2, 1,                   result.width() - 2, result.height() - 2);
		// And assign the feedback pixmap to the drag object:
		//multipleDrag->setPixmap(result, QPoint(-8, -8));
		return result;
	}
	return QPixmap();
}

bool NoteDrag::canDecode(QMimeSource *source)
{
	return source->provides(NOTE_MIME_STRING);
}

Basket* NoteDrag::basketOf(QMimeSource *source)
{
    QByteArray srcData = source->encodedData(NOTE_MIME_STRING);
	QBuffer buffer(&srcData);
	if (buffer.open(QIODevice::ReadOnly)) {
		QDataStream stream(&buffer);
		// Get the parent basket:
		quint64 basketPointer;
		stream >> (quint64&)basketPointer;
		return (Basket*)basketPointer;
	} else
		return 0;
}

Q3ValueList<Note*> NoteDrag::notesOf(QMimeSource *source)
{
    QByteArray srcData = source->encodedData(NOTE_MIME_STRING);
	QBuffer buffer(&srcData);
	if (buffer.open(QIODevice::ReadOnly)) {
		QDataStream stream(&buffer);
		// Get the parent basket:
		quint64 basketPointer;
		stream >> (quint64&)basketPointer;
		// Get the note list:
		quint64          notePointer;
		Q3ValueList<Note*> notes;
		do {
			stream >> notePointer;
			if (notePointer != 0)
				notes.append((Note*)notePointer);
		} while (notePointer);
		// Done:
		return notes;
	} else
		return Q3ValueList<Note*>();
}

Note* NoteDrag::decode(QMimeSource *source, Basket *parent, bool moveFiles, bool moveNotes)
{
    QByteArray srcData = source->encodedData(NOTE_MIME_STRING);
	QBuffer buffer(&srcData);
	if (buffer.open(QIODevice::ReadOnly)) {
		QDataStream stream(&buffer);
		// Get the parent basket:
		quint64 basketPointer;
		stream >> (quint64&)basketPointer;
		Basket *basket = (Basket*)basketPointer;
		// Get the note list:
		quint64          notePointer;
		Q3ValueList<Note*> notes;
		do {
			stream >> notePointer;
			if (notePointer != 0)
				notes.append((Note*)notePointer);
		} while (notePointer);
		// Decode the note hierarchy:
		Note *hierarchy = decodeHierarchy(stream, parent, moveFiles, moveNotes, basket);
		// In case we moved notes from one basket to another, save the source basket where notes were removed:
		basket->filterAgainDelayed(); // Delayed, because if a note is moved to the same basket, the note is not at its
		basket->save();               //  new position yet, and the call to ensureNoteVisible would make the interface flicker!!
		return hierarchy;
	} else
		return 0;
}

Note* NoteDrag::decodeHierarchy(QDataStream &stream, Basket *parent, bool moveFiles, bool moveNotes, Basket *originalBasket)
{
	quint64  notePointer;
	quint64  type;
	QString   fileName;
	QString   fullPath;
	QDateTime addedDate;
	QDateTime lastModificationDate;

	Note *firstNote    = 0; // TODO: class NoteTreeChunk
	Note *lastInserted = 0;

	do {
		stream >> notePointer;
		if (notePointer == 0)
			return firstNote;
		Note *oldNote = (Note*)notePointer;

		Note *note = 0;
		quint64 groupWidth;
		stream >> type >> groupWidth;
		if (type == NoteType::Group) {
			note = new Note(parent);
			note->setGroupWidth(groupWidth);
			quint64 isFolded;
			stream >> isFolded;
			if (isFolded)
				note->toggleFolded(/*animate=*/false);
			if (moveNotes) {
				note->setX(oldNote->x()); // We don't move groups but re-create them (every childs can to not be selected)
				note->setY(oldNote->y()); // We just set the position of the copied group so the animation seems as if the group is the same as (or a copy of) the old.
				note->setHeight(oldNote->height()); // Idem: the only use of Note::setHeight()
			}
			Note* childs = decodeHierarchy(stream, parent, moveFiles, moveNotes, originalBasket);
			if (childs) {
				for (Note *n = childs; n; n = n->next())
					n->setParentNote(note);
				note->setFirstChild(childs);
			}
		} else {
			stream >> fileName >> fullPath >> addedDate >> lastModificationDate;
			if (moveNotes) {
				originalBasket->unplugNote(oldNote);
				note = oldNote;
				if (note->basket() != parent) {
					QString newFileName = NoteFactory::createFileForNewNote(parent, "", fileName);
					note->content()->setFileName(newFileName);
					KIO::FileCopyJob *copyJob = KIO::file_move(KUrl(fullPath), KUrl(parent->fullPath() + newFileName),
					                                           /*perms=*/-1, /*override=*/true, /*resume=*/false, /*showProgressInfo=*/false);
					parent->connect(copyJob, SIGNAL(copyingDone(KIO::Job *, KUrl, KUrl, time_t, bool, bool)),
                                    parent, SLOT(slotCopyingDone2(KIO::Job *, KUrl, Kurl)));
				}
				note->setGroupWidth(groupWidth);
				note->setParentNote(0);
				note->setPrev(0);
				note->setNext(0);
				note->setParentBasket(parent);
				NoteFactory::consumeContent(stream, (NoteType::Id)type);
			} else if ( (note = NoteFactory::decodeContent(stream, (NoteType::Id)type, parent)) ) {
				note->setGroupWidth(groupWidth);
				note->setAddedDate(addedDate);
				note->setLastModificationDate(lastModificationDate);
			} else if (!fileName.isEmpty()) {
				// Here we are CREATING a new EMPTY file, so the name is RESERVED
				// (while dropping several files at once a filename cannot be used by two of them).
				// Later on, file_copy/file_move will copy/move the file to the new location.
				QString newFileName = NoteFactory::createFileForNewNote(parent, "", fileName);
				note = NoteFactory::loadFile(newFileName, (NoteType::Id)type, parent);
				KIO::FileCopyJob *copyJob;
				if (moveFiles)
					copyJob = KIO::file_move(KUrl(fullPath), KUrl(parent->fullPath() + newFileName),
					                         /*perms=*/-1, /*override=*/true, /*resume=*/false, /*showProgressInfo=*/false);
				else
					copyJob = KIO::file_copy(KUrl(fullPath), KUrl(parent->fullPath() + newFileName),
					                         /*perms=*/-1, /*override=*/true, /*resume=*/false, /*showProgressInfo=*/false);
				parent->connect(copyJob, SIGNAL(copyingDone(KIO::Job *, KUrl, KUrl, time_t, bool, bool)),
				                parent, SLOT(slotCopyingDone2(KIO::Job *, KUrl, KUrl)));
				note->setGroupWidth(groupWidth);
				note->setAddedDate(addedDate);
				note->setLastModificationDate(lastModificationDate);
			}
		}
		// Retreive the states (tags) and assign them to the note:
		if (note && note->content()) {
			quint64 statePointer;
			do {
				stream >> statePointer;
				if (statePointer)
					note->addState((State*)statePointer);
			} while (statePointer);
		}
		// Now that we have created the note, insert it:
		if (note) {
			if (!firstNote)
				firstNote = note;
			else {
				lastInserted->setNext(note);
				note->setPrev(lastInserted);
			}
			lastInserted = note;
		}
	} while (true);

	// We've done: return!
	return firstNote;
}

/** ExtendedTextDrag */

bool ExtendedTextDrag::decode(const QMimeSource *e, QString &str)
{
	Q3CString subtype("plain");
	return decode(e, str, subtype);
}

bool ExtendedTextDrag::decode(const QMimeSource *e, QString &str, Q3CString &subtype)
{
	// Get the string:
	bool ok = Q3TextDrag::decode(e, str, subtype);

	// Test if it was a UTF-16 string (from eg. Mozilla):
	if (str.length() >= 2) {
		if ((str[0] == 0xFF && str[1] == 0xFE) || (str[0] == 0xFE && str[1] == 0xFF)) {
			QByteArray utf16 = e->encodedData(QString("text/" + subtype).local8Bit());
			str = QTextCodec::codecForName("utf16")->toUnicode(utf16);
			return true;
		}
	}

	// Test if it was empty (sometimes, from GNOME or Mozilla)
	if (str.length() == 0 && subtype == "plain") {
		if (e->provides("UTF8_STRING")) {
			QByteArray utf8 = e->encodedData("UTF8_STRING");
			str = QTextCodec::codecForName("utf8")->toUnicode(utf8);
			return true;
		}
		if (e->provides("text/unicode")) { // FIXME: It's UTF-16 without order bytes!!!
			QByteArray utf16 = e->encodedData("text/unicode");
			str = QTextCodec::codecForName("utf16")->toUnicode(utf16);
			return true;
		}
		if (e->provides("TEXT")) { // local encoding
			QByteArray text = e->encodedData("TEXT");
			str = QString(text);
			return true;
		}
		if (e->provides("COMPOUND_TEXT")) { // local encoding
			QByteArray text = e->encodedData("COMPOUND_TEXT");
			str = QString(text);
			return true;
		}
	}
	return ok;
}

#include "notedrag.moc"
