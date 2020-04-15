//============================================================================
//
//% Student Name 1: Jasleen Kalsi
//% Student 1 #: 301274460
//% Student 1 userid (email): jkkalsi@sfu.ca
//
//% Student Name 2: Mohit Sharma
//% Student 2 #: 301274887
//% Student 2 userid (email): msa197@sfu.ca
//
//% Below, edit to list any people who helped you with the code in this file,
//%      or put 'None' if nobody helped (the two of) you.
//
// Helpers: TA's
//
// Also, list any resources beyond the course textbooks and the course pages on Piazza
// that you used in making your submission.
//
// Resources:  ___________
//
//%% Instructions:
//% * Put your name(s), student number(s), userid(s) in the above section.
//% * Also enter the above information in other files to submit.
//% * Edit the "Helpers" line and, if necessary, the "Resources" line.
//% * Your group name should be "P3_<userid1>_<userid2>" (eg. P3_stu1_stu2)
//% * Form groups as described at:  https://courses.cs.sfu.ca/docs/students
//% * Submit files to courses.cs.sfu.ca
//
// File Name   : myIO.cpp
// Version     : September, 2019
// Description : Wrapper I/O functions for ENSC-351
// Copyright (c) 2019 Craig Scratchley  (wcs AT sfu DOT ca)
//============================================================================


#include <unistd.h>         // for read/write/close
#include <fcntl.h>          // for open/creat
#include <sys/socket.h>     // for socketpair
#include "SocketReadcond.h"
#include <mutex>
#include <condition_variable>
#include <vector>
#include <chrono>         // std::chrono::

class threadsafe_vec
{
private:
    int count;
    int pair;
    bool Socket=true;

public:
    mutable std::mutex mut;
    std::condition_variable cond;

    threadsafe_vec::threadsafe_vec()
    {
        pair=0;
        count=0;

    }

    void decrementCount(int C)
    {
        count-=C;
    }
    void incrementCount(int C)
    {
        count+=C;
    }
    int getCount()
    {
        return count;
    }
    void setPair(int P)
    {
        pair=P;
    }
    int getPair()
    {
        return pair;
    }
    void setSocket(bool S)
    {
        Socket=S;
    }
    bool getSocket()
    {
        return Socket;
    }

};

std::vector<threadsafe_vec*> Vec;//this is our vector
std::mutex mutVec;// mutex for our vector

int mySocketpair( int domain, int type, int protocol, int des[2] )
{
    int returnVal = socketpair(domain, type, protocol, des);

    std::lock_guard<std::mutex> lk(mutVec);

    int newSize = des[1] + 1;
    if(Vec.size()<(unsigned)(des[1]))//checks if the vector needs to be resized
    {
        Vec.resize(newSize);//resize vector if needed
    }

    //create new objects, linking socket pairs
    Vec[des[0]]= new threadsafe_vec;
    Vec[des[1]]= new threadsafe_vec;

    //link the pairs toghther so they know what their pair is
    Vec[des[0]]->setPair(des[1]);
    Vec[des[1]]->setPair(des[0]);

    return returnVal;
}

int myOpen(const char *pathname, int flags, mode_t mode)
{
    int file = open(pathname, flags, mode);

    std::lock_guard<std::mutex> lk(mutVec);

    int unsigned newSize = file + 1;
    if(Vec.size() < newSize)
    {
        Vec.resize(newSize);//resize vector if needed
    }

    Vec[file]= new threadsafe_vec;
    Vec[file]->setSocket(false);//resize vector if needed

    return file;
}

int myCreat(const char *pathname, mode_t mode)
{
    int file = creat(pathname, mode);

    std::lock_guard<std::mutex> lk(mutVec);

    int unsigned newSize = file + 1;
    if(Vec.size() < newSize)
    {
        Vec.resize(newSize);//resize vector if needed
    }
    Vec[file]= new threadsafe_vec;
    Vec[file]->setSocket(false);// this is not a socket

    return file;
}

int myReadcond(int des, void * buf, int n, int min, int time, int timeout)
{
    int bytes=0;//bytes being read, but not total number of bytees
    int total=0;//total number of bytes read
    int fd=Vec[des]->getPair();

    std::unique_lock<std::mutex> lk(Vec[fd]->mut, std::defer_lock);

    if(min==0)//checking case where minimum is already zero to avoid errors
    {
        total=wcsReadcond(des, buf, n, min, time, timeout );
        Vec[fd]->decrementCount(total);//decrement count inside the class

        if(Vec[fd]->getCount()==0)
        {
            Vec[fd]->cond.notify_one();//once the count reaches zero, unlock the mutex
        }
    }

    //read one byte at a time till the minimum number
    while(total<min)
    {
        bytes=wcsReadcond(des, (uint_least8_t*)buf+total, n, 1, time, timeout );
        Vec[fd]->decrementCount(bytes);//decrement count inside the class

        if(Vec[fd]->getCount()==0)//once the count reaches zero, unlock the mutex
        {
            Vec[fd]->cond.notify_one();
        }
        total+=bytes;//increment total number of bytes being read
    }
    return total;
}

ssize_t myRead( int fildes, void* buf, size_t nbyte )
{
    // cast a call from myRead() into a call to myReadcond()
    return myReadcond(fildes, buf, nbyte, 1, 0, 0 );
}

ssize_t myWrite( int fildes, const void* buf, size_t nbyte )
{
    int writebytes=0;//number of bytes being written, which will be incremented within the class

    std::lock_guard<std::mutex> lk(Vec[fildes]->mut);//lock the mutex

    writebytes=write(fildes, buf, nbyte );
    //incrementing count of bytes written
    Vec[fildes]->incrementCount(writebytes);//increment count inside the class

    return writebytes;
}

int myClose( int fd )
{
    int file = close(fd);
    int temp=0;
    std::lock_guard<std::mutex> lk(mutVec);//lock the mutex

    if((Vec[fd]->getSocket())==false)//delete if not a socket
    {
        delete Vec[fd];//prevent overflow in memory
        return file;
    }
    else if((Vec[fd]->getSocket())==true)//delete if it is a socket
    {
        delete Vec[file];//prevent overflow in memory
        Vec[file] = 0;

        delete Vec[temp];//prevent overflow in memory
        Vec[temp] = 0;

        return file;
    }
    else//if nothing works
    {
        return file;
    }
}

int myTcdrain(int des)
{
    std::unique_lock<std::mutex> lk(Vec[des]->mut);//lock the mutex
    Vec[des]->cond.wait(lk,[des]{ return Vec[des]->getCount() == 0; });//wait for condition to be true
    return 0;
}



