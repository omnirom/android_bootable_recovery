from xml.dom import minidom
import sys
import getopt

HELP = """
  compare_xml.py [ -o file.xml ]
                   -f file.xml
                   -h - help info
"""

enfile = "en.xml"
otherfile = ""

try:
	opts, args = getopt.getopt(sys.argv[1:], "hfo:koz", ["device="])
except getopt.GetoptEror:
	print HELP
	sys.stdout.flush()
	sys.exit(2)

for opt, arg in opts:
	if opt == "-h":
		print HELP
		sys.stdout.flush()
		sys.exit()
	elif opt == "-o":
		otherfile = arg
	elif opt == "-f":
		enfile = arg

if otherfile == "":
	print HELP
	exit()

print "Comparing %s and %s" % (enfile, otherfile)
print ""

endoc = minidom.parse(enfile)
enstrings = endoc.getElementsByTagName('string')

otherdoc = minidom.parse(otherfile)
otherstrings = otherdoc.getElementsByTagName('string')

for ens in enstrings:
	found = False
	for others in otherstrings:
		if ens.attributes['name'].value == others.attributes['name'].value:
			found = True
			break
	if found == False:
		print "'%s' present in %s and not in %s" % (ens.attributes['name'].value, enfile, otherfile)

print ""

for others in otherstrings:
	found = False
	for ens in enstrings:
		if ens.attributes['name'].value == others.attributes['name'].value:
			found = True
			break
	if found == False:
		print "'%s' present in %s and not in %s" % (others.attributes['name'].value, otherfile, enfile)
