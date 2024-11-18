#!/bin/bash

# 使用 netstat 查找并关闭已存在的进程
pid=$(netstat -tlnp 2>/dev/null | grep ':9999' | awk '{print $7}' | cut -d'/' -f1)
# 或者使用 ss
# pid=$(ss -tlnp | grep ':9999' | awk '{print $6}' | cut -d',' -f2 | cut -d'=' -f2)

if [ ! -z "$pid" ]; then
    echo "Killing existing process on port 9999..."
    kill $pid
    sleep 1
fi

# 启动服务器
./liso_server