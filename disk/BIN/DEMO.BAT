@ECHO OFF
REM DEMO.BAT - a quick tour of batch file features
ECHO Hello %1! This is %0.
IF "%1"=="" ECHO (Tip: try DEMO yourname)
IF EXIST \README.TXT ECHO README.TXT is on drive C.
IF NOT EXIST \NOPE.TXT ECHO NOPE.TXT does not exist, as expected.
ECHO Running HELLO.EXE from a batch file:
HELLO from batch
IF ERRORLEVEL 1 GOTO failed
ECHO HELLO exited with code 0.
GOTO end
:failed
ECHO Something went wrong.
:end
ECHO Done.
