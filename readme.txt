1. file or folder from which begin search
2. N file search masks (* and ? wild cards) (separated with `:` symbol) - empty or `*` for search in all files
3. deep search level - 0 for infinite deep (all levels), 1 - we search only in current folder, 2 - folder and direct child subfolders. and so on
4. i not follow symlinks/reparse point during search
5. user can select use cached or non-cached io
6. search string - up to 256 symbols (or 85 raw hex bytes)
7. string can be in ansi, oem, utf-8, utf-16 encoding or raw hex bytes
8. for ansi, oem, and utf-16 we can search case sensetive or insesetive (more slow and more memory used)
9. for utf-16 i assume that string have 2*n offset from file begin (which is true for pe and plain text files)
10. for utf-8 only case sensetive search (try upcase from every position in file will be too slow)
11. for raw bytes hex search case of course have no sense
12. user can select maximum concurrent io requests count (by default 32).

i use only asynchronous io - for both enumerate folders via NtQueryDirectoryFile which support asynchronous io unlike pitiful FindFirstFileEx shell. and for Read file. so after begin io operation - i never wait in place when it finished, but continue execution (in case folder continue walk through FILE_DIRECTORY_INFORMATION entries). so instead creating several threads for synchronous enumerate folders/scan files synchronously (sure you wait this) - i implement much more power asynchronous solution. and we select here not thread count but simultaneous io requests count. during process folder (enumerate child FILE_DIRECTORY_INFORMATION entries i check current io count and if it exceeds limit - i stop enumeration and push Folder object to queue (Task::PauseFolder). later, when some io finished and simultaneous io count down - i check this queue and resume enumaration for folder Task::CheckSuspendedTasks -> Task::ResumeFolder - called from file and folder completion end.

i test only on NVMe disks where i got the best result with ~16-32 simultaneous io requests count. of course on old, not ssd disks maybe the best will be have only 1 request in time, i not check - but GUI let select you any value in range [1, 100) and test result

13. i log all errors during scan and show log window (if exist errors) after scan end
14. user ca cancel/stop scan at any time
15. GUI updated every 1 second during scan and when scan finished
16. file lists with offset within the file where the string started (we can have empty search string - in this case i will be search for files/folders only based on it name (if mask exist) but not scan it content - in this case offset of course not displayed)
17. prefix/suffix (i use up to 8 characters) i show via tooltip. for format string (hex bytes + ascii) i use CryptBinaryToString with CRYPT_STRING_HEXASCII|CRYPT_STRING_NOCRLF. for support xp (primary for demo only, which not implement CRYPT_STRING_NOCRLF flag - i hook this api call (from own exe) on xp)

18. for implement asynchronous io i bind IOCP to file/folder handles. despite BindIoCompletionCallback enough here, for best perfomance i use own fixed count thread pool (thread count == cpu count in group) and IOCP

in solution i use my generic IO class library (look in asio: io.h, io.cpp, tp.cpp) where implemented logic for objects, used asynchronous io
concrete scan logic implemented task.[h/cpp] and file.[h/cpp] with auxiliary files: "name component".h/cpp, mask.h/cpp, lineheap.h/cpp
for apply mask, despite is possible use RtlIsNameInExpression (win7+) i implemet own IsNameInExpression (in mask.cpp)
for GUI i use own generic library winz. however here very simply gui :)
for search substring i use most primitive algoritm, but maximum optimized in asm (x86/x64) code (look in code32.asm, code64.asm - strnstr, wtrnstr)

files readed chunk by chunk (64kb). first search in [buf, buf + cbChunk), then (if file have more data) last (CbStr-1) bytes copied before read buffer and next searches in [buf - cbStr + 1, buf + cbChunk) - look implementation in File::OnRead in file.cpp

asio - https://github.com/rbmm/LIB/tree/master/ASIO
