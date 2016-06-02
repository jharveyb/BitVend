#!/usr/local/bin/python

import time
import blockcypher
import RPi.GPIO as GPIO

#machine address
address = '12RQxxartkNQ7E23jwyqLoJLQbc1sdRtk8'

#BlockCypher testnet address
testaddress = 'BwgSb6rdywfZyNDYRKu97bRAEeNWDkqZuo'

unconfnum = 0
unconfval = 0
unconfavg = 0 
oldstate=[unconfval, unconfnum, unconfavg]
threshold = 210000

def checkwallet(addr, coinsym):
    details = blockcypher.get_address_details(addr, coin_symbol=coinsym)
    pendval = details['unconfirmed_balance']
    print 'pending value is ' + str(pendval)
    pendnum = details['unconfirmed_n_tx']
    print 'pending number is ' + str(pendnum)
    if pendnum == 0 or pendval == 0:
        pendavg = 0
    else:
        pendavg=pendval/pendnum
    print 'pending average is ' + str(pendavg)
    totalval = details['final_balance']
    print 'confirmed balance is ' + str(totalval)
    return [pendval, pendnum, pendavg]

GPIO.setmode(GPIO.BOARD)
GPIO.setup(12, GPIO.OUT, initial=GPIO.LOW)

#run check every 5 seconds
while True:
    state=checkwallet(testaddress, 'bcy')
    if state != oldstate:
        print 'new state is ' + str(state)
        if state[2] != 0:
            num = state[1] - oldstate[1]
            oldstate = state
            if num == 1: #one new unconfirmed
                if state[0] >= threshold: #above threshold
                    print "Sending signal to Arduino now"
                    GPIO.output(12, True)
                    time.sleep(0.1)
                    GPIO.output(12, False)
        if state[2] == 0:
            oldstate = [0, 0, 0]
        time.sleep(20)
    if state == oldstate:
        time.sleep(20)
    
    


