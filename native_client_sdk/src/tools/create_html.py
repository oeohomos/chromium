#!/usr/bin/env python
# Copyright (c) 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Creates simple HTML for running a NaCl module.

This script is designed to make the process of creating running
Native Client executables in the browers simple by creating
boilderplate a .html (and optionally a .nmf) file for a given
Native Client executable (.nexe).

If the script if given a .nexe file it will produce both html
the nmf files.  If it is given an nmf it will only create
the html file.
"""

import optparse
import os
import sys
import subprocess

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
HTML_TEMPLATE = '''\
<!DOCTYPE html>
<!--
Sample html container for embedded NaCl module.  This file was auto-generated
by the create_html tool which is part of the NaCl SDK.

The embed tag is setup with ps_stdout and ps_stderr attributes which, for
applications linked with ppapi_simple, will cause stdout and stderr to be sent
to javascript via postMessage.  Also, the postMessage listener assumes that
all messages sent via postMessage are strings to be displayed in the output
textarea.
-->
<html>
<head>
  <meta http-equiv="Pragma" content="no-cache">
  <meta http-equiv="Expires" content="-1">
  <title>%(title)s</title>

</head>
<body>
  <h2>Native Client Module: %(module_name)s</h2>
  <p>Status: <code id="status">Loading</code></p>

  <div id="listener">
    <embed id="nacl_module" name="%(module_name)s" src="%(nmf)s"
           type="application/x-nacl" ps_stdout="/dev/tty" ps_stderr="/dev/tty"
           width=640 height=480 />
  </div>

  <p>Standard output/error:</p>
  <textarea id="stdout" rows="25" cols="80">
</textarea>

  <script>
listenerDiv = document.getElementById("listener")
stdout = document.getElementById("stdout")
nacl_module = document.getElementById("nacl_module")

function updateStatus(message) {
  document.getElementById("status").innerHTML = message
}

function addToStdout(message) {
  stdout.value += message;
  stdout.scrollTop = stdout.scrollHeight;
}

function handleMessage(message) {
  addToStdout(message.data)
}

function handleCrash(event) {
  updateStatus("Crashed/exited with status: " + nacl_module.exitStatus)
}

function handleLoad(event) {
  updateStatus("Loaded")
}

listenerDiv.addEventListener("load", handleLoad, true);
listenerDiv.addEventListener("message", handleMessage, true);
listenerDiv.addEventListener("crash", handleCrash, true);
  </script>
</body>
</html>
'''


class Error(Exception):
  pass


def Log(msg):
  if Log.enabled:
    sys.stderr.write(str(msg) + '\n')
Log.enabled = False


def CreateHTML(filenames, options):
  nmf = None

  for filename in filenames:
    if not os.path.exists(filename):
      raise Error('file not found: %s' % filename)

    if not os.path.isfile(filename):
      raise Error('specified input is not a file: %s' % filename)

    basename, ext = os.path.splitext(filename)
    if ext not in ('.nexe', '.pexe', '.nmf'):
      raise Error('input file must be .nexe, .pexe or .nmf: %s' % filename)

    if ext == '.nmf':
      if len(filenames) > 1:
        raise Error('Only one .nmf argument can be specified')
      nmf = filename
    elif len(filenames) > 1 and not options.output:
      raise Error('When specifying muliple input files -o must'
                  ' also be specified.')

  htmlfile = options.output
  if not htmlfile:
    htmlfile = basename + '.html'
  basename = os.path.splitext(os.path.basename(htmlfile))[0]

  if not nmf:
    nmf = os.path.splitext(htmlfile)[0] + '.nmf'
    Log('creating nmf: %s' % nmf)
    create_nmf = os.path.join(SCRIPT_DIR, 'create_nmf.py')
    staging = os.path.dirname(nmf)
    if not staging:
      staging = '.'
    cmd = [create_nmf, '-s', staging, '-o', nmf] + filenames
    if options.verbose:
      cmd.append('-v')
    if options.debug_libs:
      cmd.append('--debug-libs')
    Log(cmd)
    try:
      subprocess.check_call(cmd)
    except subprocess.CalledProcessError:
      raise Error('create_nmf failed')

  Log('creating html: %s' % htmlfile)
  with open(htmlfile, 'w') as outfile:
    args = {}
    args['title'] = basename
    args['module_name'] = basename
    args['nmf'] = os.path.basename(nmf)
    outfile.write(HTML_TEMPLATE % args)


def main(argv):
  usage = 'Usage: %prog [options] <.nexe/.pexe or .nmf>'
  epilog = 'Example: create_html.py -o index.html my_nexe.nexe'
  parser = optparse.OptionParser(usage, description=__doc__, epilog=epilog)
  parser.add_option('-v', '--verbose', action='store_true',
                    help='Verbose output')
  parser.add_option('-d', '--debug-libs', action='store_true',
                    help='When calling create_nmf request debug libaries')
  parser.add_option('-o', '--output', dest='output',
                    help='Name of html file to write (default is '
                         'input name with .html extension)',
                    metavar='FILE')

  # To enable bash completion for this command first install optcomplete
  # and then add this line to your .bashrc:
  #  complete -F _optcomplete create_html.py
  try:
    import optcomplete
    optcomplete.autocomplete(parser)
  except ImportError:
    pass

  options, args = parser.parse_args(argv)

  if not args:
    parser.error('no input file specified')

  if options.verbose:
    Log.enabled = True

  CreateHTML(args, options)
  return 0


if __name__ == '__main__':
  try:
    rtn = main(sys.argv[1:])
  except Error, e:
    sys.stderr.write('%s: %s\n' % (os.path.basename(__file__), e))
    rtn = 1
  except KeyboardInterrupt:
    sys.stderr.write('%s: interrupted\n' % os.path.basename(__file__))
    rtn = 1
  sys.exit(rtn)
