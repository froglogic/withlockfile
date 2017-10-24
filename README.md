withlockfile
============
A Windows program for synchronising command execution.

Synopsis
--------
`withlockfile` is a small utility program which can be used to synchronise
command executions on Microsoft Windows via a lock file. This is useful to
ensure that commands accessing shared resources (and which don't synchronise
such accesses internally) always only do so sequentially.

Installation
------------
To compile the source code, Visual Studio 2008 or later is required. No extra
dependencies are imposed, and generating the executable should be a matter of
running

    cl /nologo /MT /W2 /EHsc withlockfile.cpp shlwapi.lib

Usage
-----
Simply prefix an arbitrary command line with `withlockfile <lockfile>`, e.g.
instead of running

    symstore add ... /s \\myserver\symbols

use

    withlockfile "%TEMP%\mylock.lck" symstore add ... /s \\myserver\symbols

This guards the 'symstore' invocation such that no two executions are running
at the same time, using a lock file in `%TEMP%\mylock.lck` for synchronisation.

Motivation
----------
The original use case was adding debug symbol information to a [symbol
store](http://msdn.microsoft.com/en-us/library/windows/desktop/ms680693%28v=vs.85%29.aspx).
Writing data to a symbol store is done using the
[SymStore](http://msdn.microsoft.com/en-us/library/windows/desktop/ms681417%28v=vs.85%29.aspx)
program, but the manual explains:

> SymStore does not support simultaneous transactions from multiple users. It
> is recommended that one user be designated "administrator" of the symbol
> store and be responsible for all add and del transactions.

This is a problem in case the software builds generating debug symbols may be
done concurrently on multiple build machines, and it's possible that two
(independant) builds finish at roughly the same time and hence try to update
the symbol store in parallel.

License
-------
This software is licensed under the terms of the [GNU General Public License
Version 3](https://www.gnu.org/licenses/gpl-3.0.txt).
