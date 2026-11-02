# Distributed File Sharing System

## Overview

This project implements a distributed peer-to-peer file sharing system using **C++**, consisting of:

* **Tracker** — maintains users, groups, file metadata, and peer information.
* **Client** — allows users to create/join groups, upload and download files, and manage file sharing.
* **Multiple Trackers** — tracker state is synchronized between two tracker instances.

## Project Structure

```text
P2P/
├── client/
├── tracker/
├── common/
├── tracker_info.txt
└── README.md
```

## Compilation

### Tracker

```bash
g++ -std=c++17 -pthread tracker/*.cpp common/*.cpp -o tracker_bin
```

### Client 

Install OpenSSL development libraries:

```bash
sudo apt install libssl-dev
```

Compile:

```bash
g++ -std=c++17 -pthread client/*.cpp common/*.cpp -o client_bin -lcrypto
```

## Running

Start the trackers using the tracker information file:

```bash
./tracker_bin info.txt 1
./tracker_bin info.txt 2
```

The final argument specifies the tracker number (`1` or `2`).

Start a client:

```bash
./client_bin
```

Multiple clients can be started in separate terminals.

## Configuration

`tracker_info.txt` contains the IP addresses and ports of the trackers.

Example:

```text
127.0.0.1 9001
127.0.0.1 9002
```

## Supported Commands

### User Management

```text
create user <username> <password>
login <username> <password> <port>
logout
```

### Group Management

```text
create group <group_id>
join group <group_id>
leave group <group_id>
list groups
list requests <group_id>
accept request <group_id> <username>
```

### File Operations

```text
upload file <group_id> <file_path>
list files <group_id>
download file <group_id> <file_name> <destination>
show downloads
stop share <group_id> <file_name>
```

## Features

* User registration and login
* Group creation and membership management
* Group join requests and approval
* File uploading and sharing
* File downloading between peers
* Multiple simultaneous clients
* Two-trackers synchronization
* File metadata and peer tracking
* SHA-1 based file verification
* Graceful handling of client disconnections
* Ownership transfer when a group owner leaves

## Dependencies

* C++17
* POSIX sockets
* pthreads
* OpenSSL (`libssl-dev` on Ubuntu)

## Notes

Run the trackers before starting clients. For local testing, use different terminals for the two trackers and the clients.

The client and tracker communicate using TCP sockets, while tracker synchronization maintains consistent state between the two tracker instances.
