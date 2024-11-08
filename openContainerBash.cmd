@echo off
set CONTAINER_NAME=socketProgramming
docker start %CONTAINER_NAME% > null
docker exec -it %CONTAINER_NAME% /bin/bash