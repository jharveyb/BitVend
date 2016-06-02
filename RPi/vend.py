from blockcypher import get_address_overview as details
import urllib2
import sys
import time
import os
from datetime import datetime
from types import StringType
from types import ListType


def pricefetch():
    page = urllib2.urlopen("https://blockchain.info/q/24hrprice")
    pageinfo = float(page.read())
    return pageinfo


def logwrite(string, filname):
    fil = open(filname, 'a')
    if type(string) is ListType:
        fil.writelines(string)
    if type(string) is StringType:
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
    fil = open(filname, 'a')
    fil.write(str(addr + "\n"))
    fil.close()
    if len(sys.argv) >= 2:
        if bool(sys.argv[1]) is True:
            testflag = True
            if len(sys.argv) >= 3:
                count = int(sys.argv[2])
                print count
        else:
            import RPi.GPIO as GPIO
            GPIO.setmode(GPIO.BOARD)
            GPIO.setup(12, GPIO.OUT, initial=GPIO.LOW)
    fil = open(filname, 'a')
    fil.write(str(str(testflag) + "\n"))
    fil.close()
    while count != 0:
        price = pricefetch()
        print price
        satprice = price / 1e8
        print satprice
        info = details(addr)
        curbal = info["final_balance"]
        print curbal
        fil = open(filname, 'a')
        seq = [str(price) + "\n", str(satprice) + "\n", str(curbal) + "\n"]
        fil.writelines(seq)
        fil.close()
        if curbal != oldbal:
            print "Change!"
            diff = curbal - oldbal
            fil = open(filname, 'a')
            fil.write("Satoshis sent-\n" + str(diff) + "\n")
            fil.close()
            if diff*satprice >= 0.95:
                print "Received %s satoshis!" % diff
                timex = datetime.today()
                fil = open(filname, 'a')
                fil.write("Transaction price-\n" + str(diff*satprice) + "\n")
                fil.write(timex.strftime("%j%H%M") + "\n")
                fil.close()
                if testflag is True:
                    print "Valid transaction!"
                    fil = open(filname, 'a')
                    fil.write("Valid test transaction.\n")
                    fil.close()
                else:
                    print "Registering currency!"
                    GPIO.output(12, True)
                    time.sleep(0.1)
                    GPIO.output(12, False)
                    fil = open(filname, 'a')
                    fil.write("Valid output to Arduino.\n")
                    fil.close()
        oldbal = curbal
        count -= 1
        time.sleep(20)
        

if __name__ == "__main__":
    main()
