/*
 * Utility to manipulate Quake PAK data files.
 * Copyright (C) 2015  Dennis Katsonis <dennisk (at) netspace dot net dot au>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include "pak.h"
#include "treeitem.h"


Pak::Pak() : memused(0), verbose(false),
    directoryOffset(PAK_HEADER_SIZE), directoryLength(0), thisDirectoryEntryOffset(0), numEntries(0),
    m_rootEntry("root", nullptr), loadingDir(false)
{

    file.exceptions ( std::ifstream::failbit | std::ifstream::badbit );
}

int Pak::close()
{
    if (file.is_open()) {
        try {
            file.close();
        } catch (std::istream::failure &e) {
            throw (PakException("Could not close file", "Pakqit could not close the currently open file." ));
        }
    }
    m_rootEntry.clear();
    return 0;
}

Pak::~Pak()
{
    if (file.is_open()) {
        file.close();
    }
}


int Pak::open(const char *filename)
{
    if (fexists(filename) == false) {
        try {
        file.open(filename, std::ios_base::binary | std::ios_base::out | std::ios_base::trunc);
    } catch (std::istream::failure &e) {
        throw PakException("Could not open file", filename);
        }
    pakFile = filename;
    return 0;
    }
    
    try {
            file.open(filename, std::ios_base::in  | std::ios_base::out | std::ios_base::binary);
        } catch (std::istream::failure &e) {
            throw PakException("Could not open file", filename);
        }
    
    try {
        file.read(signature.data(), signature.size());
        file.read(reinterpret_cast<char *>(&directoryOffset), sizeof(int32_t));
        file.read(reinterpret_cast<char *>(&directoryLength), sizeof(int32_t));

    } catch (std::istream::failure &e) {
        std::string message = filename;
        message +=" : Error loading file.";
        throw (PakException("Invalid file", message.c_str()));
    }
    if (std::equal(signature.begin(), signature.end(), "PACK") == 0) {
        std::string message = filename;
        message +=" is not a valid PAK file";
        throw (PakException("Invalid file", message.c_str()));
    }
    if (directoryLength % 64 != 0) {
        throw (PakException("File not valid", "Error reading directory.  File is corrupt or not a PAK file."));
    }
    numEntries = (directoryLength / 64);
    try {
        file.seekg(directoryOffset, std::ios::beg);

        for (auto x = 0; x < numEntries; ++x) {

            DirectoryEntry entry;
            file.read(entry.filename.data() , PAK_DATA_LABEL_SIZE);
            int position = entry.getPosition();
            file.read(reinterpret_cast<char *>(&position), sizeof(int32_t));
            int32_t length;
            file.read(reinterpret_cast<char *>(&length), sizeof(int32_t));
            entry.setLength(length);
            entry.setPosition(position);
            loadDir(std::move(entry));
        }
    } catch (std::istream::failure &e) {
        throw (PakException("File not valid", "Error reading directory.  File is corrupt or not a PAK file."));
    }

    pakFile = filename;

    return 0;

}

TreeItem *Pak::addChild(stringList &dirList, TreeItem *entry)
{
    if (dirList.empty()) {
        return entry;
    }

    auto x = entry->findChild(dirList.front(), true);
    if (x != nullptr) { // There are more
        dirList.erase(dirList.begin());
        x = addChild(dirList, x);
        return x;
    }
    return x;
}

void Pak::updateIndex(DirectoryEntry &entry)
{
    entry.setPosition(directoryOffset);
    directoryLength = safeAdd(directoryLength, DIRECTORY_ENTRY_SIZE);
    directoryOffset = safeAdd(directoryOffset, entry.getLength());
}

void Pak::resetPakDirectory()
{
    directoryLength = 0;
    directoryOffset = PAK_HEADER_SIZE;
    thisDirectoryEntryOffset = 0;
    m_rootEntry.traverseForEachItem(&Pak::updateIndex, this);
}


void Pak::deleteChild(std::string path)
{
    if (path.back() != '/') {  // There has to be a '/' at the end
      // to ensure that it is tokenised correctly.
      path.append("/");
    }
    TreeItem *treeToDelete = m_rootEntry.findTreeItem(path);
    if (treeToDelete == nullptr) { // It wasn't found.
        return;
    }
    if (treeToDelete == &m_rootEntry) { // We are not going
        // to delete the root folder either.
        throw PakException("Invalid directory", "Will not delete root directory.\n If you wan't an empty PAK file, just delete it.");
    }
    deleteChild(treeToDelete->paren(),treeToDelete->row());
}

void Pak::deleteChild(TreeItem *entry, int row)
{
    entry->deleteChildTree(row);
}


void Pak::deleteEntry(const std::string entry)
{ 
    TreeItem *tItem = nullptr;
    std::string entryItem;
    std::string path;
    auto slashPos = entry.find_last_of('/');

    if (slashPos == std::string::npos) {
        // If no slash, assume we are referring to a top level (root) item.
        tItem = &m_rootEntry;
	entryItem = entry;
    } else {
      // Otherwise, take the part after the slash as the entry (file), and the part before as the path.
      slashPos++;  // Advance one as we want the data after the slash.
      path = entry.substr(0, slashPos);
      entryItem = entry.substr(slashPos, entry.length());
      
      tItem = m_rootEntry.findTreeItem(path);

      if (tItem == nullptr) { // It wasn't found.
	std::string message;
	message += "Could not find path ";
	message += path;
	throw PakException("Invalid Path", message.c_str());
      }
    }
    auto row = tItem->findEntryRow(entryItem);
    
    if (row == -1) {
      std::string message;
      message += "Attempting to delete non-existant item ";
      message += entryItem;
      throw PakException("Invalid Item", message.c_str());
    }
    tItem->deleteItem(row);
}
void Pak::deleteEntry(TreeItem *root, const int row)
{
    if (root == nullptr) {
        throw PakException("Invalid directory", "Attempting to delete non-existant directory.");
    }
    root->deleteItem(row);
}


void Pak::loadDir(DirectoryEntry entry)
{
    // First, we get the position in the directory tree.
    stringList directoryList;
    clearArrayAfterNull(entry.filename);
    directoryList = tokenize(entry.filename);
    auto x = addChild(directoryList, &m_rootEntry);
    x->appendItem(entry);
}


Pak::Pak(const char *filename) : Pak()
{
    open(filename);
}

void Pak::makeDirectoryTree(TreeItem *item)
{

    for (auto x = 0; x < item->childCount(); ++x) {
#ifdef __linux
        mkdir(item->child(x)->label().c_str(), S_IRUSR | S_IWUSR | S_IXUSR);
#elif __WIN32
        mkdir(item->child(x)->label().c_str());
#elif __APPLE__
        mkdir(item->child(x)->label().c_str(), S_IRUSR | S_IWUSR | S_IXUSR);
#endif
        chdir(item->child(x)->label().c_str());
        makeDirectoryTree(item->child(x));
    }

    for (auto x = 0; x < item->size(); x++) {

        std::ofstream fout;

#ifndef CLI
        if (fexists(absoluteFileName((item->data(x).filename)).toStdString().c_str())) {
            if (confirmOverwrite(absoluteFileName((item->data(x).filename)).toStdString().c_str()) == false) {
                continue;
            }
        }
        fout.open(absoluteFileName((item->data(x).filename)).toStdString().c_str(), std::ios_base::binary | std::ios_base::out);
#else
        if (fexists(absoluteFileName((item->data(x).filename)).c_str())) {
            if (confirmOverwrite(absoluteFileName((item->data(x).filename)).c_str()) == false) {
                continue;
            }
        }
        if (verbose) {
            std::cout << "Extracting.. " << absoluteFileName((item->data(x).filename)) << "\n";
        }
        fout.open(absoluteFileName((item->data(x).filename)).c_str(), std::ios_base::binary | std::ios_base::out);
#endif
        if (item->data(x).data() == nullptr) {
            item->data(x).loadData(file);
        }
        fout.write(item->data(x).data(), item->data(x).getLength());
        fout.close();
        
    }
    chdir("..");
}


int Pak::exportPak(const char *exportPath)
{
    chdir(exportPath);
    makeDirectoryTree(&m_rootEntry);
    return 0;
}

int Pak::exportDirectory(const char *exportPath, TreeItem *item)
{
    chdir(exportPath); // If we are just exporting a single directory,
    // we will want to create it first.
#ifdef __linux
    mkdir(item->label().c_str(), S_IRUSR | S_IWUSR | S_IXUSR);
#elif __WIN32
    mkdir(item->label().c_str());
#elif __APPLE__
    mkdir(item->label().c_str(), S_IRUSR | S_IWUSR | S_IXUSR);
#endif
    chdir(item->label().c_str());
    makeDirectoryTree(item);
    return 0;
}


int Pak::writePakDir(TreeItem *item)
{
    for (auto x = item->begin(); x != item->end(); x++) {
        writeEntry(*x);
    }

    if (item->childCount()) {
        for (int x = 0; x < item->childCount(); ++x) {
            writePakDir(item->child(x));
        }
    }
    return 0;
}


int Pak::writePak(const char *filename)
{
    if (file.is_open()) {
        m_rootEntry.traverseForEachItem(&Pak::loadData, this);
        file.close();
    }

    // Close the pakFile at the end, as we will be opening a new one
    // based on the file name provided.
    resetPakDirectory();
    try {
        file.open(filename, std::ios_base::binary | std::ios_base::out | std::ios_base::trunc);

    } catch (std::istream::failure &e) {
        throw PakException("Could not open file", filename);
    }
    file.seekp(PAK_HEADER_SIZE);

    m_rootEntry.traverseForEachItem(&Pak::writeEntry, this);
    
    file.seekp(std::ios::beg);
    file.write("PACK", signature.size());
    file.write(reinterpret_cast<char *>(&directoryOffset), sizeof(int32_t));
    file.write(reinterpret_cast<char *>(&directoryLength), sizeof(int32_t));
    file.close();

    return 0;
}

int Pak::exportEntry(std::string &entryname, TreeItem* source)
{
  auto *entry = source->findEntry(entryname);
  if (entry == nullptr) {
    throw PakException("Could not find entry.", entryname.c_str());
    }

#ifndef CLI // This uses QString
    entry->exportFile ( getFileName (QString(entryname.c_str()) ).toStdString().c_str(), file );
#else
    entry->exportFile ( getFileName ( entryname ).c_str(), file );
#endif
  return 0;
}



void Pak::reset()
{
    if (file.is_open()) {
        file.close();
    }
    directoryLength = 0;
    directoryOffset = PAK_HEADER_SIZE;
    m_rootEntry.clear();
    memused = 0;

}

void Pak::writeEntry(DirectoryEntry &entry)
{
    file.seekp(entry.getPosition());
    file.write(entry.data(), entry.getLength());
    file.seekp(directoryOffset + thisDirectoryEntryOffset);
    thisDirectoryEntryOffset = safeAdd(thisDirectoryEntryOffset, DIRECTORY_ENTRY_SIZE);
    file.write(entry.filename.data(), PAK_DATA_LABEL_SIZE);
    int32_t position = entry.getPosition();
    file.write(reinterpret_cast<char *>(&position), sizeof(int32_t));
    int length = entry.getLength();
    file.write(reinterpret_cast<char *>(&length), sizeof(int32_t));
    return;
}


void Pak::loadData(DirectoryEntry &entry)
{
    if (!entry.isLoaded()) {
        entry.loadData(file);
    }

    return;
}


int Pak::addEntry(std::string path, const char *filename, TreeItem *rootItem)
{
    struct stat statbuf;
    DirectoryEntry newEntry;

#ifndef CLI // This uses QString
    path += getFileName(filename).toStdString();
#else
    path += getFileName(filename);
#endif

#ifndef NDEBUG
#ifdef CLI
    std::cout << "Adding " << filename << " as " << path << " with parent " << rootItem->label() << std::endl;
#endif
#endif


    if (path.size() > (PAK_DATA_LABEL_SIZE - 1)) {
        throw PakException("Path name too long", path.c_str());
    }


    auto endpos = std::copy(path.begin(), path.end(), newEntry.filename.data());
    for (auto pos = endpos; pos != newEntry.filename.end(); ++pos) {
        *pos = '\0';
    }

    stat(filename, &statbuf);
    auto filesize = statbuf.st_size;

    newEntry.setLength(filesize);
    newEntry.loadData(filename);
    newEntry.setPosition(directoryOffset);
    directoryOffset = safeAdd(directoryOffset, filesize);
    rootItem->appendItem(newEntry);
#ifdef CLI
    if (verbose) {
      std::cout << path << "\t" << newEntry.getLength() << " bytes \n";
    }
#endif
    return NO_ERROR;
}


int Pak::importDirectory(const char *importPath, TreeItem *rootItem)
{
    std::string currentPath;
    std::string oldPath;
    static TreeItem *initialTreeRoot = nullptr;
    DIR *directory;

    if (loadingDir == false && rootItem == nullptr) {
        rootItem = &m_rootEntry;
        initialTreeRoot = rootItem;
        loadingDir = true;
    }
    if (loadingDir == false && rootItem != nullptr) {
        currentPath = rootItem->pathLabel();
        initialTreeRoot = rootItem;
        loadingDir = true;
    }
    currentPath = rootItem->pathLabel();

#ifndef NDEBUG
#ifdef CLI
    std::cout << "Current Path " << currentPath << std::endl;
#else
    qDebug() << "Current Path " << currentPath.c_str();
#endif
#endif
    struct dirent *entry;
    struct stat statbuf;
    std::string tmp;

    if ((directory = opendir(importPath)) == NULL) {
        throw PakException("Could not open directory", importPath);
    }

    chdir(importPath);

    while ((entry = readdir(directory)) != NULL) {
        stat(entry->d_name, &statbuf);

        if (S_ISDIR(statbuf.st_mode)) {

            if (std::strcmp(".", entry->d_name) == 0 || std::strcmp("..", entry->d_name) == 0) {
                continue;
            }

            tmp = currentPath;

            tmp.append(entry->d_name);
            auto newChild = rootItem->findChild(entry->d_name, true);

            if (importDirectory(entry->d_name, newChild) == -1) {
                throw PakException("Could not open directory", importPath);
            }
        } else if (S_ISREG(statbuf.st_mode)) {
            try {
                addEntry(currentPath, entry->d_name, rootItem);
            } catch (PakException& )  {
                loadingDir = false;
                currentPath.clear();
                initialTreeRoot = nullptr;
                throw;
            }
        }
    } // end while

    if (rootItem != initialTreeRoot) chdir(".."); // We don't ascend a directory when processing the root directory.

    if (rootItem == initialTreeRoot) {
        // We've reached the end of the recursion.  Reset the string
        currentPath.clear();
        loadingDir = false;
    }

    return NO_ERROR;
}

void Pak::setVerbose(bool verbosity)
{
    verbose = verbosity;
}

#ifdef CLI
void Pak::printChild(TreeItem *item)
{

    if (item->childCount() > 0) {
        for (auto x = 0; x < item->childCount(); ++x) {
            printChild(item->child(x));
        }
    }

    for (auto x = item->begin(); x != item->end(); ++x) {
        std::cout << x->filename.data() << '\t' << x->getLength() << " bytes.\n";
    }
    return;
}
#endif

TreeItem *Pak::rootEntry()
{
    return &m_rootEntry;
}

std::fstream &Pak::getFileHandle()
{
    return file;
}



