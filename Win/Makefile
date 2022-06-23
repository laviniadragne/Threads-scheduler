CC = cl
CFLAGS = /nologo /W3
LINK = link

.PHONY: build
build: libscheduler.dll

libscheduler.dll: so_scheduler.obj
	$(LINK) /nologo /dll /out:$@ /implib:libscheduler.lib $**

so_scheduler.obj: so_scheduler.c
	$(CC) $(CFLAGS) /Fo$@ /c $**

clean:
	del /Q /F *.obj *.lib *.dll *.exp