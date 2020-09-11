/****************************************************************************
**
** Copyright (C) 2020 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the tools applications of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:GPL-EXCEPT$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/
#include "manifestwriter.h"

#include "config.h"
#include "examplenode.h"
#include "generator.h"
#include "qdocdatabase.h"

#include <QtCore/qmap.h>
#include <QtCore/qset.h>
#include <QtCore/qxmlstream.h>

QT_BEGIN_NAMESPACE

/*!
    \internal

    For each attribute in a list of attributes, checks if the attribute is
    found in \a usedAttributes. If it is not found, issues a warning that
    the example with \a name is missing that attribute.
 */
void warnAboutUnusedAttributes(const QStringList &usedAttributes, const QString &name)
{
    const QStringList attributesToWarnFor = { "imageUrl", "projectPath" };

    for (const auto &attribute : attributesToWarnFor) {
        if (!usedAttributes.contains(attribute))
            Location().warning(name + ": missing attribute " + attribute);
    }
}

/*!
    \internal

    Write the description element. The description for an example is set
    with the \brief command. If no brief is available, the element is set
    to "No description available".
 */

void writeDescription(QXmlStreamWriter *writer, const ExampleNode *example)
{
    writer->writeStartElement("description");
    const Text brief = example->doc().briefText();
    if (!brief.isEmpty())
        writer->writeCDATA(brief.toString());
    else
        writer->writeCDATA(QString("No description available"));
    writer->writeEndElement(); // description
}

/*!
    \internal

    Returns a list of \a files that Qt Creator should open for the \a exampleName.
 */
QMap<int, QString> getFilesToOpen(const QStringList &files, const QString &exampleName)
{
    QMap<int, QString> filesToOpen;
    for (const QString &file : files) {
        QFileInfo fileInfo(file);
        QString fileName = fileInfo.fileName().toLower();
        // open .qml, .cpp and .h files with a
        // basename matching the example (project) name
        // QMap key indicates the priority -
        // the lowest value will be the top-most file
        if ((fileInfo.baseName().compare(exampleName, Qt::CaseInsensitive) == 0)) {
            if (fileName.endsWith(".qml"))
                filesToOpen.insert(0, file);
            else if (fileName.endsWith(".cpp"))
                filesToOpen.insert(1, file);
            else if (fileName.endsWith(".h"))
                filesToOpen.insert(2, file);
        }
        // main.qml takes precedence over main.cpp
        else if (fileName.endsWith("main.qml")) {
            filesToOpen.insert(3, file);
        } else if (fileName.endsWith("main.cpp")) {
            filesToOpen.insert(4, file);
        }
    }

    return filesToOpen;
}

/*!
    \internal
    \brief Writes the lists of files to open for the example.

    Writes out the \a filesToOpen and the full \a installPath through \a writer.
 */
void writeFilesToOpen(QXmlStreamWriter &writer, const QString &installPath,
                      const QMap<int, QString> &filesToOpen)
{
    for (auto it = filesToOpen.constEnd(); it != filesToOpen.constBegin();) {
        writer.writeStartElement("fileToOpen");
        if (--it == filesToOpen.constBegin()) {
            writer.writeAttribute(QStringLiteral("mainFile"), QStringLiteral("true"));
        }
        writer.writeCharacters(installPath + it.value());
        writer.writeEndElement();
    }
}

/*!
    \class ManifestWriter
    \internal
    \brief The ManifestWriter is responsible for writing manifest files.
 */
ManifestWriter::ManifestWriter()
{
    Config &config = Config::instance();
    m_project = config.getString(CONFIG_PROJECT);
    m_outputDirectory = config.getOutputDir();
    m_qdb = QDocDatabase::qdocDB();

    const QString prefix = CONFIG_QHP + Config::dot + m_project + Config::dot;
    m_manifestDir =
            QLatin1String("qthelp://") + config.getString(prefix + QLatin1String("namespace"));
    m_manifestDir += QLatin1Char('/') + config.getString(prefix + QLatin1String("virtualFolder"))
            + QLatin1Char('/');
    readManifestMetaContent();
    m_examplesPath = config.getString(CONFIG_EXAMPLESINSTALLPATH);
    if (!m_examplesPath.isEmpty())
        m_examplesPath += QLatin1Char('/');
}

void ManifestWriter::processManifestMetaContent(const QString &fullName,
                                                QStringList *usedAttributes,
                                                QXmlStreamWriter *writer)
{
    if (!usedAttributes || !writer)
        return;

    for (const auto &index : m_manifestMetaContent) {
        const auto &names = index.names;
        for (const QString &name : names) {
            bool match;
            int wildcard = name.indexOf(QChar('*'));
            switch (wildcard) {
            case -1: // no wildcard, exact match
                // matches manifestmeta.highlighted.names
                match = (fullName == name);
                break;
            case 0: // '*' matches all
                // matches manifestmeta.module.names = *
                match = true;
                break;
            default: // match with wildcard at the end
                match = fullName.startsWith(name.left(wildcard));
            }
            if (match) {
                m_tags += index.tags;
                const auto attributes = index.attributes;
                for (const QString &attribute : attributes) {
                    const QLatin1Char div(':');
                    QStringList attrList = attribute.split(div);
                    if (attrList.count() == 1)
                        attrList.append(QStringLiteral("true"));
                    QString attrName = attrList.takeFirst();
                    if (!usedAttributes->contains(attrName)) {
                        writer->writeAttribute(attrName, attrList.join(div));
                        *usedAttributes << attrName;
                    }
                }
            }
        }
    }
}

/*!
  This function outputs one or more manifest files in XML.
  They are used by Creator.
 */
void ManifestWriter::generateManifestFiles()
{
    generateManifestFile("examples", "example");
    generateManifestFile("demos", "demo");
    m_qdb->exampleNodeMap().clear();
    m_manifestMetaContent.clear();
}

/*!
  This function is called by generateManifestFiles(), once
  for each manifest file to be generated. \a manifest is the
  type of manifest file.
 */
void ManifestWriter::generateManifestFile(const QString &manifest, const QString &element)
{
    const ExampleNodeMap &exampleNodeMap = m_qdb->exampleNodeMap();
    if (exampleNodeMap.isEmpty())
        return;

    bool demos = false;
    if (manifest == QLatin1String("demos"))
        demos = true;

    bool proceed = false;
    for (const auto &example : exampleNodeMap) {
        if (demos == example->name().startsWith("demos")) {
            proceed = true;
            break;
        }
    }
    if (!proceed)
        return;

    const QString outputFileName = manifest + "-manifest.xml";
    QFile outputFile(m_outputDirectory + QLatin1Char('/') + outputFileName);
    if (!outputFile.open(QFile::WriteOnly | QFile::Text))
        return;

    QXmlStreamWriter writer(&outputFile);
    writer.setAutoFormatting(true);
    writer.writeStartDocument();
    writer.writeStartElement("instructionals");
    writer.writeAttribute("module", m_project);
    writer.writeStartElement(manifest);

    QStringList usedAttributes;
    for (const auto &example : exampleNodeMap.values()) {
        if (demos) {
            if (!example->name().startsWith("demos"))
                continue;
        } else if (example->name().startsWith("demos"))
            continue;

        const QString installPath = retrieveExampleInstallationPath(example);

        // attributes that are always written for the element
        usedAttributes.clear();
        usedAttributes << "name"
                       << "docUrl";

        writer.writeStartElement(element);
        writer.writeAttribute("name", example->title());
        QString docUrl = m_manifestDir + Generator::fileBase(example) + ".html";
        writer.writeAttribute("docUrl", docUrl);

        if (!example->projectFile().isEmpty()) {
            writer.writeAttribute("projectPath", installPath + example->projectFile());
            usedAttributes << "projectPath";
        }
        if (!example->imageFileName().isEmpty()) {
            writer.writeAttribute("imageUrl", m_manifestDir + example->imageFileName());
            usedAttributes << "imageUrl";
        }

        const QString fullName = m_project + QLatin1Char('/') + example->title();

        processManifestMetaContent(fullName, &usedAttributes, &writer);
        warnAboutUnusedAttributes(usedAttributes, example->name());
        writeDescription(&writer, example);
        addWordsFromModuleNamesAsTags();
        includeTagsAddedWithMetaCommand(example);
        addTitleWordsToTags(example);
        cleanUpTags();
        writeTagsElement(&writer);

        const QString exampleName = example->name().mid(example->name().lastIndexOf('/') + 1);
        const auto files = example->files();
        const QMap<int, QString> filesToOpen = getFilesToOpen(files, exampleName);
        writeFilesToOpen(writer, installPath, filesToOpen);

        writer.writeEndElement(); // example
    }

    writer.writeEndElement(); // examples
    writer.writeEndElement(); // instructionals
    writer.writeEndDocument();
    outputFile.close();
}

/*!
    \internal

    Populates the tags and writes the tags element, then clears the tags member.
 */
void ManifestWriter::writeTagsElement(QXmlStreamWriter *writer)
{
    if (!writer || m_tags.isEmpty())
        return;

    writer->writeStartElement("tags");
    QStringList sortedTags = m_tags.values();
    sortedTags.sort();
    writer->writeCharacters(sortedTags.join(","));
    writer->writeEndElement(); // tags
    m_tags.clear();
}

/*!
    \internal

    Clean up tags, exclude invalid and common words.
 */
void ManifestWriter::cleanUpTags()
{
    QSet<QString> cleanedTags;

    for (auto tag : m_tags) {
        if (tag.at(0) == '(')
            tag.remove(0, 1).chop(1);
        if (tag.endsWith(QLatin1Char(':')))
            tag.chop(1);

        if (tag.length() < 2 || tag.at(0).isDigit() || tag.at(0) == '-'
            || tag == QLatin1String("qt") || tag == QLatin1String("the")
            || tag == QLatin1String("and") || tag.startsWith(QLatin1String("example"))
            || tag.startsWith(QLatin1String("chapter")))
            continue;
        cleanedTags << tag;
    }
    m_tags = cleanedTags;
}

/*!
    \internal

    Add the example's title as tags.
 */
void ManifestWriter::addTitleWordsToTags(const ExampleNode *example)
{
    if (!example)
        return;

    const auto &titleWords = example->title().toLower().split(QLatin1Char(' '));
    m_tags += QSet<QString>(titleWords.cbegin(), titleWords.cend());
}

/*!
    \internal

    Add words from module name as tags
    QtQuickControls -> qt,quick,controls
    QtOpenGL -> qt,opengl
    QtQuick3D -> qt,quick3d
 */
void ManifestWriter::addWordsFromModuleNamesAsTags()
{
    QRegularExpression re("([A-Z]+[a-z0-9]*(3D|GL)?)");
    int pos = 0;
    QRegularExpressionMatch match;
    while ((match = re.match(m_project, pos)).hasMatch()) {
        m_tags << match.captured(1).toLower();
        pos = match.capturedEnd();
    }
}

/*!
    \internal

    Include tags added via \meta {tag} {tag1[,tag2,...]}
    within \example topic.
 */
void ManifestWriter::includeTagsAddedWithMetaCommand(const ExampleNode *example)
{
    if (!example)
        return;

    const QStringMultiMap *metaTagMap = example->doc().metaTagMap();
    if (metaTagMap) {
        for (const auto &tag : metaTagMap->values("tag")) {
            const auto &tagList = tag.toLower().split(QLatin1Char(','));
            m_tags += QSet<QString>(tagList.constBegin(), tagList.constEnd());
        }
    }
}

/*!
  Reads metacontent - additional attributes and tags to apply
  when generating manifest files, read from config.

  The manifest metacontent map is cleared immediately after
  the manifest files have been generated.
 */
void ManifestWriter::readManifestMetaContent()
{
    Config &config = Config::instance();
    const QStringList names =
            config.getStringList(CONFIG_MANIFESTMETA + Config::dot + QStringLiteral("filters"));

    for (const auto &manifest : names) {
        ManifestMetaFilter filter;
        QString prefix = CONFIG_MANIFESTMETA + Config::dot + manifest + Config::dot;
        filter.names = config.getStringSet(prefix + QStringLiteral("names"));
        filter.attributes = config.getStringSet(prefix + QStringLiteral("attributes"));
        filter.tags = config.getStringSet(prefix + QStringLiteral("tags"));
        m_manifestMetaContent.append(filter);
    }
}

/*!
  Retrieve the install path for the \a example as specified with
  the \\meta command, or fall back to the one defined in .qdocconf.
 */
QString ManifestWriter::retrieveExampleInstallationPath(const ExampleNode *example) const
{
    QString installPath;
    if (example->doc().metaTagMap())
        installPath = example->doc().metaTagMap()->value(QLatin1String("installpath"));
    if (installPath.isEmpty())
        installPath = m_examplesPath;
    if (!installPath.isEmpty() && !installPath.endsWith(QLatin1Char('/')))
        installPath += QLatin1Char('/');

    return installPath;
}

QT_END_NAMESPACE
