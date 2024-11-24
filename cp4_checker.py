#!/usr/bin/python
from socket import *
import sys,os,time,signal,errno,tempfile
sys.stderr=tempfile.TemporaryFile()

# python3 cp4_checker.py 127.0.0.1 9999 

def test_week4(request, s1, s2, s3):
    buf_size = 1024
    try:
        s1.send(request.encode('utf-8'))
        s2.send(request.encode('utf-8'))
        s3.send(request.encode('utf-8'))
        recv_string1 = s1.recv(buf_size).decode('utf-8')
        recv_string2 = s2.recv(buf_size).decode('utf-8')
        recv_string3 = s3.recv(buf_size).decode('utf-8')
        if recv_string1.find('HTTP/1.1 200 OK')<0 \
        or recv_string2.find('HTTP/1.1 200 OK')<0 \
        or recv_string2.find('HTTP/1.1 200 OK')<0: 
            print("[problem] Please realize this week\'s task on the basis of last week.")
            return 0.5
    except TimeoutError:
        print("Timeout reached")
        return 0        
    return 1

def handle_timeout(signum, frame):
    raise TimeoutError(os.strerror(errno.ETIME))

if len(sys.argv) < 3:
    sys.stderr.write('Usage: %s <ip> <port>\n' % (sys.argv[0]))
    sys.exit(1)

os.system('tmux new -s checker -d "./liso_server > test.log 2>&1"')
time.sleep(2)

TIMEOUT=5
signal.signal(signal.SIGALRM, handle_timeout)
signal.alarm(TIMEOUT)

serverHost = gethostbyname(sys.argv[1])
serverPort = int(sys.argv[2])
s1 = socket(AF_INET, SOCK_STREAM); s1.connect((serverHost, serverPort))
s2 = socket(AF_INET, SOCK_STREAM); s2.connect((serverHost, serverPort))
s3 = socket(AF_INET, SOCK_STREAM); s3.connect((serverHost, serverPort))

REQUEST = \
'GET / HTTP/1.1\r\nHost: www.cs.cmu.edu\r\nConnection: keep-alive\r\n\
Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8\
User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/39.0.2171.99 Safari/537.36\
Accept-Encoding: gzip, deflate, sdch\r\nAccept-Language: en-US,en;q=0.8\r\n\r\n'

result = test_week4(REQUEST, s1, s2, s3)
scores = 100 * result
s1.close(); s2.close(); s3.close()
signal.alarm(0)
os.system('tmux kill-session -t checker')

\
print("=== Server Log ===")
try:
    with open("test.log", "r") as f:
        print(f.read())
except:
    print("No log file found")
print("=== End Server Log ===")

print("{\"scores\": {\"lab4\": %.2f}}"%scores)