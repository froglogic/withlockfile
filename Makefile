withlockfile.exe: withlockfile.cpp
	cl /nologo /MT /W2 /EHsc withlockfile.cpp shlwapi.lib

clean:
	del *.exe *.obj


