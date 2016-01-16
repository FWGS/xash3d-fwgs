import sys
import struct
import os

#dummy class for stuffing the file headers into
class FileEntry:
    pass

#arguments are source directory, then target filename e.g. "pak1.pak"
rootdir = sys.argv[1]
pakfilename = sys.argv[2]

pakfile = open(pakfilename,"wb")

#write a dummy header to start with
pakfile.write(struct.Struct("<4s2l").pack(b"PACK",0,0))

#walk the directory recursively, add the files and record the file entries
offset = 12
fileentries = []
for root, subFolders, files in os.walk(rootdir):
    for file in files:
        entry = FileEntry()
        impfilename = os.path.join(root,file)
        entry.filename = os.path.relpath(impfilename,rootdir).replace("\\","/")
        if(entry.filename.startswith(".git")):continue
	print "pak: "+entry.filename
        with open(impfilename, "rb") as importfile:
            pakfile.write(importfile.read())
            entry.offset = offset
            entry.length = importfile.tell()
            offset = offset + entry.length
        fileentries.append(entry)
tablesize = 0

#after all the file data, write the list of entries
for entry in fileentries:
    pakfile.write(struct.Struct("<56s").pack(entry.filename.encode("ascii")))
    pakfile.write(struct.Struct("<l").pack(entry.offset))
    pakfile.write(struct.Struct("<l").pack(entry.length))
    tablesize = tablesize + 64

#return to the header and write the values correctly
pakfile.seek(0)
pakfile.write(struct.Struct("<4s2l").pack(b"PACK",offset,tablesize))
