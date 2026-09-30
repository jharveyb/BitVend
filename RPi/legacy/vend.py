from blockcypher import get_address_overview as details
import urllib2
import sys
import time
import os
from datetime import datetime
from types import StringType
from types import ListType
# import pdb
import RPi.GPIO as GPIO


def pricefetch(price):
    try:
        page = urllib2.urlopen("https://blockchain.info/q/24hrprice")
        pageinfo = float(page.read())
        return pageinfo
    except:
        return price


def logwrite(string, filname):
    fil = open(filname, 'a')
    if type(string) is ListType:
        fil.writelines(string)
    elif type(string) is StringType:
        fil.write(string)
    fil.close()


def main():
    direc = str(os.getcwd() + "/" + "log")
    timef = datetime.today()
    curtime = timef.strftime("%j%H%M")
    filname = direc + curtime
    print curtime
    addr = '1BLAUugHT67PJnJARvV7AADjxFB6pDQ75i'
    count = float('inf')
    oldbal = float('inf')
    price = 540
    testflag = False
    # pdb.set_trace()
    logwrite(str(addr + "\n"), filname)
    if len(sys.argv) >= 2:
        if bool(sys.argv[1]) is True:
            testflag = True
            if len(sys.argv) >= 3:
                count = int(sys.argv[2])
                print count
        # else:
            # import RPi.GPIO as GPIO
    GPIO.setmode(GPIO.BOARD)
    GPIO.setup(12, GPIO.OUT, initial=GPIO.LOW)
    logwrite(str(str(testflag) + "\n"), filname) 
    while count != 0:
        price = pricefetch(price)
        print price
        satprice = price / 1e8
        print satprice
        try:
	    info = details(addr)
            curbal = info["final_balance"]
            print curbal
            seq = [str(price) + "\n", str(satprice) + "\n", str(curbal) + "\n"]
            logwrite(seq, filname)
            if curbal != oldbal:
                print "Change!"
                diff = curbal - oldbal
                logwrite(str("Satoshis sent-\n" + str(diff) + "\n"), filname)
                if diff*satprice >= 0.95:
                    print "Received %s satoshis!" % diff
                    timex = datetime.today()
                    gseq = ["Transaction price-\n" + str(diff*satprice) + "\n", timex.strftime("%j%H%M") + "\n"]
                    logwrite(gseq, filname)
                    if testflag is True:
                        print "Valid transaction!"
                        logwrite("Valid test transaction.\n", filname)
                    else:
                        print "Registering currency!"
                        GPIO.output(12, True)
                        time.sleep(0.1)
                        GPIO.output(12, False)
                        logwrite("Valid output to Arduino.\n", filname)
            oldbal = curbal
            count -= 1
            time.sleep(20)
	except:
	    logwrite("API call failed!", filname)
            time.sleep(20) 
        

if __name__ == "__main__":
    main()
