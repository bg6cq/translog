#!/usr/bin/python

import socket
import sys
import os

if len(sys.argv) < 5:
    print 'Usage: python %s <HostName> <PortNumber> <Password> <FileToSend> [ file_new_name ]' % (sys.argv[0])
    print 'FileToSend - means read from stdin'
    sys.exit();

host=sys.argv[1]
port=int(sys.argv[2])
pass_word=sys.argv[3]
file_name=sys.argv[4]
if len(sys.argv) == 6:
	file_new_name=sys.argv[5]
else:
	file_new_name=file_name

try:
        s=socket.socket(socket.AF_INET, socket.SOCK_STREAM)
except socket.error, msg:
        print 'Failed to creat socket. Error code: ' + str(msg[0]) + ' Error message: ' + msg[1]
        sys.exit();
try:
        host_ip=socket.gethostbyname(host)
except socket.gaierror:
        print 'Host name could not be resolved. Exiting...'
        sys.exit();

print 'IP address of ' + host + ' is ' + host_ip + ' .'

try:
    s.connect((host_ip, port)) 
except socket.error, (value,message):
    if s:
        s.close();
    print 'Socket connection is not established!\t' + message
    sys.exit(1);

print 'Socket connected to ' + host + ' on IP ' + host_ip + ' port ' + str(port) + '.'

s.send('PASS '+pass_word+'\n')
data = s.recv(100)
print 'S', data
if data[0:2] != 'OK':
    print 'exit with return code 255'
    sys.exit(-1)

if file_name =="-":
    CHUNKSIZE=1024
    s.send('FILE '+file_new_name+'\n')
    while True:
        bytes_read = sys.stdin.read(CHUNKSIZE)
        if bytes_read:
            s.send(bytes_read);
        else:
            break;
    print 'OK, exit with return code 0'
    sys.exit(0)
else:
    file_size = os.path.getsize(file_name)
    CHUNKSIZE=1024*1024
    file = open(file_name, "rb")
    s.send('FILE '+file_new_name+' '+str(file_size)+'\n')
    if file_size == 0:
        print '0 size file, just exit'
        sys.exit(0)
    try:
        while True:
            bytes_read = file.read(CHUNKSIZE)
            if bytes_read:
                s.send(bytes_read);
            else:
                break;
    finally:
        file.close()
    data = s.recv(100)
    print 'S', data
    if data[0:2] == 'OK':
        print 'OK, exit with return code 0'
        sys.exit(0)
    else:
        sys.exit(-1)
