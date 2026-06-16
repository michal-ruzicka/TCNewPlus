@echo off
rem Use UTF-8 encoded script and console
chcp 65001 >nul
set LANG=en_US.UTF-8
@echo on

@echo off
rem
rem Sync files from one folder to another
rem
@echo on

rclone sync ^
	--dry-run ^
	--config "" ^
	-v --progress ^
	--inplace --links --fix-case --track-renames --create-empty-src-dirs ^
	--delete-excluded --delete-during ^
	"%USERPROFILE%\Downloads\Some Input Folder" ^
	"%USERPROFILE%\Downloads\Some Output Folder"

@echo off
rem
rem Wait for key press
rem
pause
