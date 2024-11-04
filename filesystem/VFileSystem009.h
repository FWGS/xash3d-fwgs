/*
VFileSystem009.h - C++ interface for filesystem_stdio
Copyright (C) 2022-2023 Xash3D FWGS contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.
*/

#ifndef VFILESYSTEM009_H
#define VFILESYSTEM009_H

// exported from dwarf
typedef enum {
	FILESYSTEM_SEEK_HEAD    = 0,
	FILESYSTEM_SEEK_CURRENT = 1,
	FILESYSTEM_SEEK_TAIL    = 2,
} FileSystemSeek_t; /* size: 4 */

typedef enum {
	FILESYSTEM_WARNING_QUIET	     = 0,
	FILESYSTEM_WARNING_REPORTUNCLOSED    = 1,
	FILESYSTEM_WARNING_REPORTUSAGE       = 2,
	FILESYSTEM_WARNING_REPORTALLACCESSES = 3,
} FileWarningLevel_t; /* size: 4 */

typedef void * FileHandle_t; /* size: 4 */
typedef int FileFindHandle_t; /* size: 4 */
typedef int WaitForResourcesHandle_t; /* size: 4 */

class IBaseInterface
{
public:
	virtual ~IBaseInterface() {}
};

class IFileSystem : public IBaseInterface {
public:
	virtual void Mount() = 0; /* linkage=_ZN11IFileSystem5MountEv */

	virtual void Unmount() = 0; /* linkage=_ZN11IFileSystem7UnmountEv */

	virtual void RemoveAllSearchPaths() = 0; /* linkage=_ZN11IFileSystem20RemoveAllSearchPathsEv */

	virtual void AddSearchPath(const char *, const char *) = 0; /* linkage=_ZN11IFileSystem13AddSearchPathEPKcS1_ */

	virtual bool RemoveSearchPath(const char *) = 0; /* linkage=_ZN11IFileSystem16RemoveSearchPathEPKc */

	virtual void RemoveFile(const char *, const char *) = 0; /* linkage=_ZN11IFileSystem10RemoveFileEPKcS1_ */

	virtual void CreateDirHierarchy(const char *, const char *) = 0; /* linkage=_ZN11IFileSystem18CreateDirHierarchyEPKcS1_ */

	virtual bool FileExists(const char *) = 0; /* linkage=_ZN11IFileSystem10FileExistsEPKc */

	virtual bool IsDirectory(const char *) = 0; /* linkage=_ZN11IFileSystem11IsDirectoryEPKc */

	virtual FileHandle_t Open(const char *, const char *, const char *) = 0; /* linkage=_ZN11IFileSystem4OpenEPKcS1_S1_ */

	virtual void Close(FileHandle_t) = 0; /* linkage=_ZN11IFileSystem5CloseEPv */

	virtual void Seek(FileHandle_t, int, FileSystemSeek_t) = 0; /* linkage=_ZN11IFileSystem4SeekEPvi16FileSystemSeek_t */

	virtual unsigned int Tell(FileHandle_t) = 0; /* linkage=_ZN11IFileSystem4TellEPv */

	virtual unsigned int Size(FileHandle_t) = 0; /* linkage=_ZN11IFileSystem4SizeEPv */

	virtual unsigned int Size(const char *) = 0; /* linkage=_ZN11IFileSystem4SizeEPKc */

	virtual long int GetFileTime(const char *) = 0; /* linkage=_ZN11IFileSystem11GetFileTimeEPKc */

	virtual void FileTimeToString(char *, int, long int) = 0; /* linkage=_ZN11IFileSystem16FileTimeToStringEPcil */

	virtual bool IsOk(FileHandle_t) = 0; /* linkage=_ZN11IFileSystem4IsOkEPv */

	virtual void Flush(FileHandle_t) = 0; /* linkage=_ZN11IFileSystem5FlushEPv */

	virtual bool EndOfFile(FileHandle_t) = 0; /* linkage=_ZN11IFileSystem9EndOfFileEPv */

	virtual int Read(void *, int, FileHandle_t) = 0; /* linkage=_ZN11IFileSystem4ReadEPviS0_ */

	virtual int Write(const void *, int, FileHandle_t) = 0; /* linkage=_ZN11IFileSystem5WriteEPKviPv */

	virtual char * ReadLine(char *, int, FileHandle_t) = 0; /* linkage=_ZN11IFileSystem8ReadLineEPciPv */

	virtual int FPrintf(FileHandle_t, char *, ...) = 0; /* linkage=_ZN11IFileSystem7FPrintfEPvPcz */

	virtual void * GetReadBuffer(FileHandle_t, int *, bool) = 0; /* linkage=_ZN11IFileSystem13GetReadBufferEPvPib */

	virtual void ReleaseReadBuffer(FileHandle_t, void *) = 0; /* linkage=_ZN11IFileSystem17ReleaseReadBufferEPvS0_ */

	virtual const char * FindFirst(const char *, FileFindHandle_t *, const char *) = 0; /* linkage=_ZN11IFileSystem9FindFirstEPKcPiS1_ */

	virtual const char * FindNext(FileFindHandle_t) = 0; /* linkage=_ZN11IFileSystem8FindNextEi */

	virtual bool FindIsDirectory(FileFindHandle_t) = 0; /* linkage=_ZN11IFileSystem15FindIsDirectoryEi */

	virtual void FindClose(FileFindHandle_t) = 0; /* linkage=_ZN11IFileSystem9FindCloseEi */

	virtual void GetLocalCopy(const char *) = 0; /* linkage=_ZN11IFileSystem12GetLocalCopyEPKc */

	virtual const char * GetLocalPath(const char *, char *, int) = 0; /* linkage=_ZN11IFileSystem12GetLocalPathEPKcPci */

	virtual char * ParseFile(char *, char *, bool *) = 0; /* linkage=_ZN11IFileSystem9ParseFileEPcS0_Pb */

	virtual bool FullPathToRelativePath(const char *, char *) = 0; /* linkage=_ZN11IFileSystem22FullPathToRelativePathEPKcPc */

	virtual bool GetCurrentDirectory(char *, int) = 0; /* linkage=_ZN11IFileSystem19GetCurrentDirectoryEPci */

	virtual void PrintOpenedFiles() = 0; /* linkage=_ZN11IFileSystem16PrintOpenedFilesEv */

	virtual void SetWarningFunc(void (*)(const char *, ...)) = 0; /* linkage=_ZN11IFileSystem14SetWarningFuncEPFvPKczE */

	virtual void SetWarningLevel(FileWarningLevel_t) = 0; /* linkage=_ZN11IFileSystem15SetWarningLevelE18FileWarningLevel_t */

	virtual void LogLevelLoadStarted(const char *) = 0; /* linkage=_ZN11IFileSystem19LogLevelLoadStartedEPKc */

	virtual void LogLevelLoadFinished(const char *) = 0; /* linkage=_ZN11IFileSystem20LogLevelLoadFinishedEPKc */

	virtual int HintResourceNeed(const char *, int) = 0; /* linkage=_ZN11IFileSystem16HintResourceNeedEPKci */

	virtual int PauseResourcePreloading() = 0; /* linkage=_ZN11IFileSystem23PauseResourcePreloadingEv */

	virtual int ResumeResourcePreloading() = 0; /* linkage=_ZN11IFileSystem24ResumeResourcePreloadingEv */

	virtual int SetVBuf(FileHandle_t, char *, int, long int) = 0; /* linkage=_ZN11IFileSystem7SetVBufEPvPcil */

	virtual void GetInterfaceVersion(char *, int) = 0; /* linkage=_ZN11IFileSystem19GetInterfaceVersionEPci */

	virtual bool IsFileImmediatelyAvailable(const char *) = 0; /* linkage=_ZN11IFileSystem26IsFileImmediatelyAvailableEPKc */

	virtual WaitForResourcesHandle_t WaitForResources(const char *) = 0; /* linkage=_ZN11IFileSystem16WaitForResourcesEPKc */

	virtual bool GetWaitForResourcesProgress(WaitForResourcesHandle_t, float *, bool *) = 0; /* linkage=_ZN11IFileSystem27GetWaitForResourcesProgressEiPfPb */

	virtual void CancelWaitForResources(WaitForResourcesHandle_t) = 0; /* linkage=_ZN11IFileSystem22CancelWaitForResourcesEi */

	virtual bool IsAppReadyForOfflinePlay(int) = 0; /* linkage=_ZN11IFileSystem24IsAppReadyForOfflinePlayEi */

	virtual bool AddPackFile(const char *, const char *) = 0; /* linkage=_ZN11IFileSystem11AddPackFileEPKcS1_ */

	virtual FileHandle_t OpenFromCacheForRead(const char *, const char *, const char *) = 0; /* linkage=_ZN11IFileSystem20OpenFromCacheForReadEPKcS1_S1_ */

	virtual void AddSearchPathNoWrite(const char *, const char *) = 0; /* linkage=_ZN11IFileSystem20AddSearchPathNoWriteEPKcS1_ */

	virtual long int GetFileModificationTime(const char *) = 0; /* linkage=_ZN11IFileSystem23GetFileModificationTimeEPKc */
};

#endif // VFILESYSTEM009_H
