#script used to make sure DESIGNDOC 
import os

f=open('DESIGNDOC', 'r')
f_out=open('DESIGNDOC2','w')
line=f.readline();
count_num=0
while(line):
	count_num=count_num+1
	print "accessing line "+str(count_num)
	splits=line.split(" ")
	outputList=[]
	outputStr=""
	num=0
	for s in splits:
		if num+len(s)>76:
			outputList.append(outputStr+'\n')
			num=len(s)
			outputStr=s
		else:
			outputStr=outputStr+' '+s
			num=num+len(s)+1
	for elem in outputList:
		f_out.write(elem)
	f_out.write(outputStr)
	line=f.readline()
f.close()
f_out.close()

os.remove("./DESIGNDOC")
os.rename("DESIGNDOC2", "DESIGNDOC")
		
