from blockcypher import get_address_overview as details
import urllib2
# import sys
import time
import os
from datetime import datetime
from types import StringType
from types import ListType
# import pdb
import RPi.GPIO as GPIO


# price fetcher, need to update to use BC.I API, returns price
def pricefetch(price, filname):
    try:
        page = urllib2.urlopen("https://blockchain.info/q/24hrprice")
        pageinfo = float(page.read())
        return pageinfo
    except:
        logwrite("Price update failed!\n", filname)
        return price


# nice logging
def logwrite(string, filname):
    fil = open(filname, 'a')
    if type(string) is ListType:
        fil.writelines(string)
    elif type(string) is StringType:
        fil.write(string)
    fil.close()


# timekeeping, returns time in desired format
def timekeeper():
    timenow = datetime.today()
    welltime = timenow.strftime("%j%H%M")
    return welltime 


# price handling, returns pulse number
def pricer(diff, satprice):
    usdval = diff*satprice
    usdval += 0.08
    pulseval = 0 
    while usdval > pulseval:
        if usdval > pulseval + 1:
            pulseval += 1
            continue
        else:
            break
    return pulseval


def main():

    # init. for logfile & GPIO
    direc = str(os.getcwd() + "/" + "log")
    curtime = timekeeper()
    filname = direc + curtime
    addr = '1BLAUugHT67PJnJARvV7AADjxFB6pDQ75i'
    oldbal = float('inf')
    pricecount = 20
    price = 650
    GPIO.setmode(GPIO.BOARD)
    GPIO.setup(12, GPIO.OUT, initial=GPIO.LOW)
    
    # main loop, should be >= 45 seconds (!)
    while True:

        # price update section, should run every 15 mins.
        if pricecount > 0:
            pricecount -= 1
        if pricecount == 0:
            price = pricefetch(price, filname)
        satprice = float(price) / float(1e8)
        
        # balance checker
        try:
            info = details(addr)
            curbal = info["final_balance"]
        except:
            logwrite("Address update failed!\n", filname)
        
        # payment handler 
        if curbal != oldbal:
            timex = timekeeper()
            diff = curbal - oldbal
            logwrite([str(diff), " - ", str(diff*satprice), " - ", timex, "\n"], filname)
            pulsenum = pricer(diff, satprice)
            logwrite(["Queueing ", str(pulsenum), " writes to Arduino.", "\n"], filname)
            while pulsenum > 0:
                GPIO.output(12, True)
                time.sleep(0.1)
                GPIO.output(12, False)
                time.sleep(0.1)
                pulsenum -= 1
        oldbal = curbal
        time.sleep(45)
        

if __name__ == "__main__":
    main()
