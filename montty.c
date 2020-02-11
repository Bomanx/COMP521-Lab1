//
// Created by Boman Yang on 1/31/20.
//

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <threads.h>
#include <terminals.h>
#include <hardware.h>

#define MAX_BUFFER_SIZE 1024
//#define NUM_TERMINALS 4

//void WDR(int term, char c);

//to control WriteDataRegister
static cond_id_t WDR_lock[NUM_TERMINALS];
//to control WriteTerminal
static cond_id_t WT_num_lock[NUM_TERMINALS];
static cond_id_t WT_writing_lock[NUM_TERMINALS];
//to control readers on ReadTerminal
static cond_id_t RT_num_lock[NUM_TERMINALS];
int RT_num[NUM_TERMINALS];
int WDR_count[NUM_TERMINALS];
//to control ReadTermianl only happens after there is something to read in the inputbuffer
static cond_id_t RT_input_buffer_lock[NUM_TERMINALS];

//echo buffer
char echo_buffer[NUM_TERMINALS][MAX_BUFFER_SIZE];
int echo_buffer_write[NUM_TERMINALS];
int echo_buffer_read[NUM_TERMINALS];
int echo_buffer_count[NUM_TERMINALS];
//to handle the newline character in the output buffer
bool WT_newline[NUM_TERMINALS];
//indicating that the WriteDataRegister is busy can cannot be called now
bool WDRing[NUM_TERMINALS];

//input buffer
char input_buffer[NUM_TERMINALS][MAX_BUFFER_SIZE];
int input_buffer_write[NUM_TERMINALS];
int input_buffer_read[NUM_TERMINALS];

//output buffer
char output_buffer[NUM_TERMINALS][MAX_BUFFER_SIZE];
int output_buffer_write[NUM_TERMINALS];
int output_buffer_read[NUM_TERMINALS];
int output_buffer_count[NUM_TERMINALS];
int WT_count[NUM_TERMINALS];

//statistics
static struct termstat stat[NUM_TERMINALS];
static cond_id_t statLock;
//to ensure mutual exclusion of WriteDataRegister
static bool statTransmit;

int term_started[NUM_TERMINALS];

extern int InitTerminal(int term) {
    Declare_Monitor_Entry_Procedure();

    //to ensure the init is only called once
    if(term_started[term] == 1){
        //terminal already started
        return -1;
    }
    WDR_lock[term] = CondCreate();
    WDR_count[term] = 0;
    WT_count[term] = 0;
    RT_input_buffer_lock[term] = CondCreate();
    WT_num_lock[term] = CondCreate();
    WT_writing_lock[term] = CondCreate();
    RT_num_lock[term] = CondCreate();
    RT_num[term] = 0;

    echo_buffer_count[term] = 0;
    echo_buffer_read[term] = 0;
    echo_buffer_write[term] = 0;
    WDRing[term] = false;
    WT_newline[term] = false;

    input_buffer_read[term] = 0;
    input_buffer_write[term] = 0;

    output_buffer_read[term] = 0;
    output_buffer_write[term] = 0;
    output_buffer_count[term] = 0;

    term_started[term] = 1;

    InitHardware(term);

    return 0;
}

extern int TerminalDriverStatistics(struct termstat * stats){
    Declare_Monitor_Entry_Procedure();
    statTransmit = true;
    int i;
    for(i = 0; i < NUM_TERMINALS; i++){
        stats[i].tty_in = stat[i].tty_in;
        stats[i].tty_out = stat[i].tty_out;
        stats[i].user_in = stat[i].user_in;
        stats[i].user_in = stat[i].user_in;
        term_started[i] = 0;
    }
    CondSignal(statLock);
    return 0;
}

extern int InitTerminalDriver(){
    Declare_Monitor_Entry_Procedure();
    int i;
    statLock = CondCreate();
    for(i = 0; i < NUM_TERMINALS; i++){
        stat[i].tty_in = 0;
        stat[i].tty_out = 0;
        stat[i].user_in = 0;
        stat[i].user_out = 0;
    }
    return 0;
}

extern void ReceiveInterrupt(int term) {
    Declare_Monitor_Entry_Procedure();
    //printf("ReceiveInterrupt\n");
    char c = ReadDataRegister(term);
    stat[term].tty_in++;
    //dealing with backspace
    if(c == '\b' || c == '\177'){
        //dealing with backspace
        //for echo buffer, put a 3-char sequence
        echo_buffer[term][echo_buffer_write[term]++] = '\b';
        echo_buffer[term][echo_buffer_write[term]++] = ' ';
        echo_buffer[term][echo_buffer_write[term]++] = '\b';
        echo_buffer_count[term] += 3;

        //nothing special for input buffer
        input_buffer[term][input_buffer_write[term]++] = '\b';
    }
    else if(c == '\r' || c =='\n'){
        echo_buffer[term][echo_buffer_write[term]++] = '\r';
        echo_buffer[term][echo_buffer_write[term]++] = '\n';
        echo_buffer_count[term] += 2;

        input_buffer[term][input_buffer_write[term]++] = '\n';
        //notify ReadTermianl that there is enough stuff to read from input buffer
        CondSignal(RT_input_buffer_lock[term]);
    }else{
        echo_buffer[term][echo_buffer_write[term]++] = c;
        echo_buffer_count[term]++;

        input_buffer[term][input_buffer_write[term]++] = c;
        //input_buffer_count[term]++;
    }

    if(!WDRing[term] && echo_buffer_count[term] > 0){
        //only trigger the very first WriteDataRegister when WRD is not busy
        if(WT_count[term] == 0){
            WDRing[term] = true;
            WriteDataRegister(term, echo_buffer[term][echo_buffer_read[term]++]);
            stat[term].tty_out++;
            echo_buffer_count[term]--;
        }
    }
}

extern void TransmitInterrupt(int term) {
    Declare_Monitor_Entry_Procedure();
    //TransmitInterrupt can do only one WDR a time
    if(echo_buffer_count[term] > 0){
        //make sure echo buffer is always first to be dealt with
        WDRing[term] = true;
        WriteDataRegister(term, echo_buffer[term][echo_buffer_read[term]++]);
        stat[term].tty_out++;
        echo_buffer_count[term]--;
    }
    else if(output_buffer_count[term] > 0){
        //if nothing to echo, deal with output buffer
        WDRing[term] = true;
        stat[term].user_in++;
        char c = output_buffer[term][output_buffer_read[term]];
        if(c == '\n'){
            //user a boolean var to write \r\n sequence
            if(!WT_newline[term]){
                WriteDataRegister(term, '\r');
                stat[term].tty_out++;
                WT_newline[term] = true;
            }else{
                WT_newline[term] = false;
                WriteDataRegister(term, '\n');
                stat[term].tty_out++;
                output_buffer_read[term]++;
                output_buffer_count[term]--;
            }
        }else{
            WriteDataRegister(term, c);
            stat[term].tty_out++;
            output_buffer_read[term]++;
            output_buffer_count[term]--;
        }
    }
    else{
        //nothing to echo and nothing to output, release the WDR lock and WT lock
        CondSignal(WDR_lock[term]);
        WDRing[term] = false;
        WDR_count[term]--;
        CondSignal(WT_writing_lock[term]);
    }
}

extern int WriteTerminal(int term, char * buf, int buflen) {
    Declare_Monitor_Entry_Procedure();
    if(term < 0 || term >= NUM_TERMINALS || term_started[term] == 0){
        return -1;
    }
    //only one WriteTerminal is allowed
    while(WT_count[term] > 0){
        CondWait(WT_num_lock[term]);
    }
    WT_count[term]++;
    //When there is stuff to echo, WT waits
    while(echo_buffer_count[term] > 0){
        CondWait(WDR_lock[term]);
    }
    //When the output buffer from previous WT call is not finished, wait
    while(output_buffer_count[term] > 0){
        CondWait(WT_writing_lock[term]);
    }
    if(buflen > MAX_BUFFER_SIZE){
        buflen = MAX_BUFFER_SIZE;
    }
    //memcpy the buf into output buffer, and trigger the first WDT,
    // and let transmitInturrupt deal with the rest of output buffer
    memcpy(&output_buffer[term], buf, buflen);
    output_buffer_write[term] = buflen;
    output_buffer_read[term] = 0;
    output_buffer_count[term] = buflen;
    WriteDataRegister(term, output_buffer[term][0]);
    stat[term].tty_out++;
    output_buffer_read[term] = 1;
    output_buffer_count[term]--;

    //Notify lock for waiting WT
    WT_count[term]--;
    CondSignal(WT_num_lock[term]);

    return buflen;
}

extern int ReadTerminal(int term, char * buf, int buflen) {
    Declare_Monitor_Entry_Procedure();
    if(term < 0 || term >= NUM_TERMINALS || term_started[term] == 0){
        return -1;
    }
    //only one ReadTerminal call can be allowed at a time
    while(RT_num[term] > 0){
        CondWait(RT_num_lock[term]);
    }
    RT_num[term]++;
    //when there is nothing in the input buffer, wait
    while(input_buffer_read[term] >= input_buffer_write[term]){
        CondWait(RT_input_buffer_lock[term]);
    }
    int newBufReader, i;
    newBufReader = 0;
    //read from input buffer
    for(i = 0; i < buflen; i++){
        char c = input_buffer[term][input_buffer_read[term]++];
        if(input_buffer_read[term] >= input_buffer_write[term]){
            break;
        }
        else if(c == '\n'){
            buf[newBufReader++] = '\n';
            stat[term].user_out++;
            break;
        }
        else if((c == '\r' || c == '\177') && newBufReader > 0){
            stat[term].user_out--;
            newBufReader--;
        }
        else{
            buf[newBufReader++] = c;
            stat[term].user_out++;
        }
    }
    //release the lock for other ReadTerminal calls
    CondSignal(RT_num_lock[term]);
    RT_num[term]--;
    return newBufReader;
}
//
//void WDR(int term, char c){
//    while(WDR_count[term] > 0){
//        CondWait(WDR_lock[term]);
//    }
//    WDR_count[term]++;
//    WriteDataRegister(term, c);
//}