/*=============================================================================

  Library: XNAT/Core

  Copyright (c) University College London,
    Centre for Medical Image Computing

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

=============================================================================*/

#include "ctkXnatFile.h"

#include "ctkXnatException.h"
#include "ctkXnatObjectPrivate.h"
#include "ctkXnatSession.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QFile>

const QString ctkXnatFile::FILE_NAME = "Name";
const QString ctkXnatFile::FILE_TAGS = "file_tags";
const QString ctkXnatFile::FILE_FORMAT = "file_format";
const QString ctkXnatFile::FILE_CONTENT = "file_content";

//----------------------------------------------------------------------------
class ctkXnatFilePrivate : public ctkXnatObjectPrivate
{
public:

  ctkXnatFilePrivate()
  : ctkXnatObjectPrivate()
  {
  }

  void reset()
  {
  }

  QString localFilePath;
};


//----------------------------------------------------------------------------
ctkXnatFile::ctkXnatFile(ctkXnatObject* parent, const QString& schemaType)
: ctkXnatObject(*new ctkXnatFilePrivate(), parent, schemaType)
{
}

//----------------------------------------------------------------------------
ctkXnatFile::~ctkXnatFile()
{
}

//----------------------------------------------------------------------------
void ctkXnatFile::setName(const QString &name)
{
  this->setProperty(FILE_NAME, name);
}

//----------------------------------------------------------------------------
QString ctkXnatFile::name() const
{
  return this->property(FILE_NAME);
}

//----------------------------------------------------------------------------
void ctkXnatFile::setFileFormat(const QString &fileFormat)
{
  this->setProperty(FILE_FORMAT, fileFormat);
}

//----------------------------------------------------------------------------
QString ctkXnatFile::fileFormat() const
{
  return this->property(FILE_FORMAT);
}

//----------------------------------------------------------------------------
void ctkXnatFile::setFileContent(const QString &fileContent)
{
  this->setProperty(FILE_CONTENT, fileContent);
}

//----------------------------------------------------------------------------
QString ctkXnatFile::fileContent() const
{
  return this->property(FILE_CONTENT);
}

//----------------------------------------------------------------------------
void ctkXnatFile::setFileTags(const QString &fileTags)
{
  this->setProperty(FILE_TAGS, fileTags);
}

//----------------------------------------------------------------------------
QString ctkXnatFile::fileTags() const
{
  return this->property(FILE_TAGS);
}

//----------------------------------------------------------------------------
void ctkXnatFile::setLocalFilePath(const QString &filePath)
{
  Q_D(ctkXnatFile);
  d->localFilePath = filePath;
}

//----------------------------------------------------------------------------
QString ctkXnatFile::localFilePath() const
{
  Q_D(const ctkXnatFile);
  return d->localFilePath;
}

//----------------------------------------------------------------------------
QString ctkXnatFile::resourceUri() const
{
  return QString("%1/files/%2").arg(parent()->resourceUri(), this->name());
}

//----------------------------------------------------------------------------
void ctkXnatFile::reset()
{
  ctkXnatObject::reset();
}

//----------------------------------------------------------------------------
void ctkXnatFile::fetchImpl()
{
  // Does not make sense to fetch a file
}

//----------------------------------------------------------------------------
void ctkXnatFile::downloadImpl(const QString& filename)
{
  QString query = this->resourceUri();
  this->session()->download(filename, query);
}

//----------------------------------------------------------------------------
void ctkXnatFile::saveImpl()
{
  Q_D(ctkXnatFile);
  QString query = this->resourceUri();
  QString filename = this->localFilePath();

  QFile file(filename);

  if (!file.exists())
  {
    QString msg = "Error uploading file! ";
    msg.append(QString("File \"%1\" does not exist!").arg(filename));
    throw ctkXnatException(msg);
  }

  // Creating the update query
  query.append(QString("?%1=%2").arg("xsi:type", this->schemaType()));
  const QMap<QString, QString>& properties = this->properties();
  QMapIterator<QString, QString> itProperties(properties);
  while (itProperties.hasNext())
  {
    itProperties.next();

    // Do not append these file specific properties since they require a slightly
    // different key for uploading a file (e.g. instead of "file_format" only "format")
    if (itProperties.key() == FILE_TAGS || itProperties.key() == FILE_FORMAT ||
        itProperties.key() == FILE_CONTENT)
      continue;

    query.append(QString("&%1=%2").arg(itProperties.key(), itProperties.value()));
  }
  query.append(QString("&%1=%2").arg("format", this->fileFormat()));
  query.append(QString("&%1=%2").arg("content", this->fileContent()));
  query.append(QString("&%1=%2").arg("tags", this->fileTags()));

  // TODO May be flag for setting overwrite and not doing this automatically
  if (this->exists())
    query.append(QString("&%1=%2").arg("overwrite", true));

  // Flag needed for file upload
  query.append(QString("&%1=%2").arg("inbody", "true"));

  this->session()->upload(filename, query);

  // Validating the file upload by requesting the catalog XML
  // of the parent resource. Unfortunately for XNAT versions <= 1.6.4
  // this is the only way to get the file's MD5 hash form the server.
  QString md5Query = this->parent()->resourceUri();
  QUuid md5ID = this->session()->httpGet(md5Query);
  QList<QVariantMap> result = this->session()->httpSync(md5ID);

  QString md5ChecksumRemote ("0");
  // Newly added files are usually at the end of the catalog
  // and hence at the end of the result list. So iterating backwards
  // is for performance reasons.
  QList<QVariantMap>::const_iterator it = result.constEnd()-1;
  while (it != result.constBegin()-1)
  {
    QVariantMap::const_iterator it2 = (*it).find(this->name());
    if (it2 != (*it).constEnd())
    {
      md5ChecksumRemote = it2.value().toString();
      break;
    }
    --it;
  }

  if (file.open(QFile::ReadOnly) && md5ChecksumRemote != "0")
  {
    QCryptographicHash hash(QCryptographicHash::Md5);
    // TODO Do this in case of Qt5
    //if (hash.addData(&file))
    hash.addData(file.readAll());
    QString md5ChecksumLocal(hash.result().toHex());
    // Retrieving the md5 checksum on the server and comparing
    // it with the local file md5 sum
    if (md5ChecksumLocal != md5ChecksumRemote)
    {
      // Remove corrupted file from server
      this->erase();
      throw ctkXnatException("Upload failed! An error occurred during file upload.");
    }
  }
  else
  {
    qWarning()<<"Could not validate file upload!";
  }
  // End file validation

}
