from xml.dom import minidom
import sys
import getopt

# language helper
#
# by Ethan Yonker (Dees_Troy)
#
# This script reads the English and supplied other language files and
# compares the 2 and reorders and rewrites the other language to a new
# XML file such that all the strings are placed in the same order as
# the English file. It will place commented out string lines for items
# that are not present in the new file and will not include any strings
# in the new file that are no longer present in the English source.
# There is also a version tag that may be compared if present between
# the English and other language in case a translation string changes.



# this helps us avoid ascii unicode errors when writing the final XML
def toprettyxml(xdoc, encoding):
    #"""Return a pretty-printed XML document in a given encoding."""
    unistr = xdoc.toprettyxml().replace(u'<?xml version="1.0" ?>',
                          u'<?xml version="1.0" encoding="%s"?>' % encoding)
    return unistr.encode(encoding, 'xmlcharrefreplace')

HELP = """
  language_helper.py   -o file.xml    other language to compre to English
                     [ -f file.xml ]  output file (defaults to new.xml)
                       -h             help info
"""

enfile = "../gui/theme/common/languages/en.xml"
otherfile = ""
outfile = "new.xml"

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
		outfile = arg

if otherfile == "":
	print HELP
	exit()

print "Comparing %s and %s" % (enfile, otherfile)
print ""

# Open English
endoc = minidom.parse(enfile)

# Open other language
otherdoc = minidom.parse(otherfile)
otherstrings = otherdoc.getElementsByTagName('string')

# create minidom-document
doc = minidom.Document()

# language tag
language = doc.createElement('language')
doc.appendChild(language)

# display tag (name of the language that shows in the GUI)
otherlang = ""
otherdisplay = otherdoc.getElementsByTagName('display')
for disnode in otherdisplay:
	if disnode.nodeType == disnode.ELEMENT_NODE:
		language.appendChild(disnode)
		otherlang = disnode.firstChild.data
		print otherlang

# resources
resources = doc.createElement('resources')
language.appendChild(resources)

enres = endoc.getElementsByTagName('resources')
for resnode in enres:
	resc = resnode.childNodes
	for child in resc:
		if child.nodeType == child.ELEMENT_NODE:
			if child.tagName != "string":
				otherres = otherdoc.getElementsByTagName('resources')
				found = False
				for othernode in otherres:
					otherresc = othernode.childNodes
					for otherchild in otherresc:
						if otherchild.nodeType == otherchild.ELEMENT_NODE:
							if otherchild.tagName == child.tagName:
								if otherchild.attributes['name'].value == child.attributes['name'].value:
									found = True
									resources.appendChild(otherchild)
									break
					if found == True:
						break
				if found == False:
					print "Failed to find %s in %s, using what we got from English" % (child.toxml(), otherlang)
					resources.appendChild(child)
			else:
				found = False
				for others in otherstrings:
					if child.attributes['name'].value == others.attributes['name'].value:
						found = True
						enver = "1"
						if child.hasAttribute('version'):
							enver = child.attributes['version'].value
						otherver = "1"
						if others.hasAttribute('version'):
							otherver = others.attributes['version'].value
						if enver != otherver:
							ver_err = "English has version " + enver + " but " + otherlang + " has version " + otherver + " for '" + child.attributes['name'].value + "'"
							print ver_err
							version_comment = doc.createComment(ver_err)
							resources.appendChild(version_comment)
							resources.appendChild(others)
						else:
							resources.appendChild(others)
						break
				if found == False:
					print "'%s' present in English and not in %s" % (child.attributes['name'].value, otherlang)
					notfound_err = "NOT FOUND " + child.toxml()
					notfound_comment = doc.createComment(notfound_err)
					resources.appendChild(notfound_comment)
		elif child.nodeType == child.COMMENT_NODE:
			resources.appendChild(child)

# Done, output the xml to a file
file_handle = open(outfile,"wb")
itspretty = toprettyxml(doc, "utf-8")
file_handle.write(itspretty)
file_handle.close()
