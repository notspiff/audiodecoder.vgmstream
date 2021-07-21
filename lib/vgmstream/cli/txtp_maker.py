#!/usr/bin/env python3
from __future__ import division
import argparse, subprocess, zlib, os, re, sys, fnmatch, logging as log

#******************************************************************************
# TXTP MAKER
#
# Creates .txtp from lists of files, mainly one .txtp per subsong
#******************************************************************************

class Cli(object):
    def _parse(self):
        description = (
            "Makes TXTP from files in folders"
        )
        epilog = (
            "examples:\n"
            "  %(prog)s bgm.fsb -in -fcm 2 -fms 5.0\n"
            "  - make .txtp for subsongs with at least 2 channels and 5 seconds\n\n"
            "  %(prog)s *.scd -r -fd -l 2\n"
            "  - make .txtp for all .scd in subdirs, ignoring dupes, one .txtp per 2ch\n\n"
            "  %(prog)s *.sm1 -fne .+STREAM[.]SS[0-9]$\n"
            "  - make .txtp for all .sm1 excluding subsongs ending with 'STREAM.SS0..9'\n\n"
            "  %(prog)s samples.bnk -fni ^bgm.?\n"
            "  - make .txtp for in .bnk including only subsong names that start with 'bgm'\n\n"
            "  %(prog)s * -r -fss 1\n"
            "  - make .txtp for all files in subdirs with at least 1 subsong\n"
            "    (ignores formats without subsongs)\n\n"
            "  %(prog)s *.fsb -n \"{fn}<__{ss}>< [{in}]>\" -z 4 -o\n"
            "  - make .txtp for all fsb, adding subsongs and stream name if they exist\n\n"
        )

        p = argparse.ArgumentParser(description=description, epilog=epilog, formatter_class=argparse.RawTextHelpFormatter)
        p.add_argument('files', help="Files to get (wildcards work)", nargs='+')
        p.add_argument('-r',  dest='recursive', help="Create .txtp in base folder from data in subfolders", action='store_true')
        p.add_argument('-c',  dest='cli', help="Set path to CLI (default: auto)")
        p.add_argument('-d',  dest='subdir', help="Set subdir inside .txtp (where file will reside)")
        p.add_argument('-n',  dest='base_name', help=("Define (name).txtp, that can be formatted using:\n"
                                                      "- {filename}|{fn}=filename without extension\n"
                                                      "- {subsong}|{ss}=subsong number)\n"
                                                      "- {internal-name}|{in}=internal stream name\n"
                                                      "- {if}=internal name or filename if not found\n"
                                                      "* may be inside <...> for conditional text\n"))
        p.add_argument('-z',  dest='zero_fill', help="Zero-fill subsong number (default: auto per subsongs)", type=int)
        p.add_argument('-ie', dest='no_internal_ext', help="Remove internal name's extension if any", action='store_true')
        p.add_argument('-m',  dest='mini_txtp', help="Create mini-txtp", action='store_true')
        p.add_argument('-o',  dest='overwrite', help="Overwrite existing .txtp\n(beware when using with internal names alone)", action='store_true')
        p.add_argument('-O',  dest='overwrite_rename', help="Rename rather than overwriting", action='store_true')
        p.add_argument('-l',  dest='layers', help="Create .txtp per subsong layers, every N channels", type=int)
        p.add_argument('-fd', dest='test_dupes', help="Skip .txtp that point to duplicate streams (slower)", action='store_true')
        p.add_argument('-fcm', dest='min_channels', help="Filter by min channels", type=int)
        p.add_argument('-fcM', dest='max_channels', help="Filter by max channels", type=int)
        p.add_argument('-frm', dest='min_sample_rate', help="Filter by min sample rate", type=int)
        p.add_argument('-frM', dest='max_sample_rate', help="Filter by max sample rate", type=int)
        p.add_argument('-fsm', dest='min_seconds', help="Filter by min seconds (N.N)", type=float)
        p.add_argument('-fsM', dest='max_seconds', help="Filter by max seconds (N.N)", type=float)
        p.add_argument('-fss', dest='min_subsongs', help="Filter min subsongs\n(1 filters formats incapable of subsongs)", type=int)
        p.add_argument('-fni', dest='include_regex', help="Filter by REGEX including matches of subsong name")
        p.add_argument('-fne', dest='exclude_regex', help="Filter by REGEX excluding matches of subsong name")
        p.add_argument('-nsc',dest='no_semicolon', help="Remove semicolon names (for songs with multinames)", action='store_true')
        p.add_argument('-v', dest='log_level', help="Verbose log level (off|debug|info, default: info)", default='info')
        return p.parse_args()

    def start(self):
        args = self._parse()
        if not args.files:
            return
        Logger(args).setup_cli()
        App(args).start()

#******************************************************************************

class _GuiLogHandler(log.Handler):
    def __init__(self, txt):
        log.Handler.__init__(self)
        self._txt = txt

    def emit(self, message):
        msg = self.format(message)
        self._txt.config(state='normal')
        self._txt.insert('end', msg + '\n')
        self._txt.config(state='disabled')

class Logger(object):
    def __init__(self, cfg):
        levels = {
            'info': log.INFO,
            'debug': log.DEBUG,
        }
        self.level = levels.get(cfg.log_level, log.ERROR)

    def setup_cli(self):
        log.basicConfig(level=self.level, format='%(message)s')

    def setup_gui(self, txt):
        log.basicConfig(level=self.level, format='%(message)s', handlers=[_GuiLogHandler(txt)])

#******************************************************************************

class Cr32Helper(object):

    def __init__(self, cfg):
        self.cfg = cfg
        self.crc32_map = {}
        self.last_dupe = False

    def get_crc32(self, filename):
        buf_size = 0x8000
        with open(filename, 'rb') as file:
            buf = file.read(buf_size)
            crc32 = 0
            while len(buf) > 0:
                crc32 = zlib.crc32(buf, crc32)
                buf = file.read(buf_size)
        return crc32 & 0xFFFFFFFF 

    def update(self, filename):
        self.last_dupe = False
        if self.cfg.test_dupes == 0:
            return
        if not os.path.exists(filename):
            return

        crc32_str = format(self.get_crc32(filename),'08x')
        if (crc32_str in self.crc32_map):
            self.last_dupe = True
            return
        self.crc32_map[crc32_str] = True

        return

    def is_last_dupe(self):
        return self.last_dupe

#******************************************************************************

# Makes .txtp (usually 1 but may do N) from a CLI output + subsong
class TxtpMaker(object):

    def __init__(self, cfg, output_b, rename_map):
        self.cfg = cfg

        self.output = str(output_b).replace("\\r","").replace("\\n","\n")
        self.channels = self._get_value("channels: ")
        self.sample_rate = self._get_value("sample rate: ")
        self.num_samples = self._get_value("stream total samples: ")
        self.stream_count = self._get_value("stream count: ")
        self.stream_index = self._get_value("stream index: ")
        self.stream_name = self._get_text("stream name: ")

        if self.channels <= 0 or self.sample_rate <= 0:
            raise ValueError('Incorrect command result')

        self.stream_seconds = self.num_samples / self.sample_rate
        self.ignorable = self._is_ignorable(cfg)
        self.rename_map = rename_map

    def __str__(self):
        return str(self.__dict__)

    def _get_string(self, str, full=False):
        find_pos = self.output.find(str)
        if (find_pos == -1):
            return None
        cut_pos = find_pos + len(str)
        str_cut = self.output[cut_pos:]
        if full:
            return str_cut.split("\n")[0].strip()
        else:
            return str_cut.split()[0].strip()

    def _get_text(self, str):
        return self._get_string(str, full=True)

    def _get_value(self, str):
        res = self._get_string(str)
        if not res:
           return 0
        return int(res)

    def is_ignorable(self):
        return self.ignorable

    def _is_ignorable(self, cfg):
        if cfg.min_channels and self.channels < cfg.min_channels:
            return True
        if cfg.max_channels and self.channels > cfg.max_channels:
            return True
        if cfg.min_sample_rate and self.sample_rate < cfg.min_sample_rate:
            return True
        if cfg.max_sample_rate and self.sample_rate > cfg.max_sample_rate:
            return True
        if cfg.min_seconds and self.stream_seconds < cfg.min_seconds:
            return True
        if cfg.max_seconds and self.stream_seconds > cfg.max_seconds:
            return True
        if cfg.min_subsongs and self.stream_count < cfg.min_subsongs:
            return True
        if cfg.exclude_regex and self.stream_name:
            p = re.compile(cfg.exclude_regex)
            if p.match(self.stream_name) is not None:
                return True
        if cfg.include_regex and self.stream_name:
            p = re.compile(cfg.include_regex)
            if p.match(self.stream_name) is None:
                return True
        return False

    def _get_stream_mask(self, layer):
        if layer + self.cfg.layers > self.channels:
            loops = self.channels - self.cfg.layers
        else:
            loops = self.cfg.layers + 1

        mask = '#C'
        for ch in range(1, loops):
            mask += str(layer + ch) + ','
        return mask[:-1]

    def _clean_stream_name(self):
        if not self.stream_name:
            return None

        txt = self.stream_name
        # remove paths #todo maybe config/replace?
        pos = txt.rfind('\\')
        if pos >= 0:
            txt = txt[pos+1:]
        pos = txt.rfind('/')
        if pos >= 0:
            txt = txt[pos+1:]

        # remove bad chars
        badchars = ['%', '*', '?', ':', '\"', '|', '<', '>']
        for badchar in badchars:
            txt = txt.replace(badchar, '_')

        if not self.cfg.no_internal_ext:
            pos = txt.rfind(".")
            if pos >= 0:
                txt = txt[:pos]

        if self.cfg.no_semicolon:
            pos = txt.find(";")
            if pos >= 0:
                txt = txt[:pos].strip()

        return txt

    def _write(self, outname, line):
        outname += '.txtp'

        cfg = self.cfg
        if cfg.overwrite_rename and os.path.exists(outname):
            if outname in self.rename_map:
                rename_count = self.rename_map[outname]
            else:
                rename_count = 0
            self.rename_map[outname] = rename_count + 1
            outname = outname.replace(".txtp", "_%08i.txtp" % (rename_count))

        if not cfg.overwrite and os.path.exists(outname):
            raise ValueError('TXTP exists in path: ' + outname)

        ftxtp = open(outname,"w+")
        if line:
            ftxtp.write(line)
        ftxtp.close()

        log.debug("created: " + outname)
        return
        
    def make(self, filename_path, filename_clean):
        cfg = self.cfg
        total_done = 0

        if self.is_ignorable():
            return total_done

        # write plain (name).txtp when no subsongs
        if self.stream_count <= 1:
            index = None
        else:
            index = str(self.stream_index) #str to avoid falsy 0
            if cfg.zero_fill is None or cfg.zero_fill < 0:
                index = index.zfill(len(str(self.stream_count)))
            else:
                index = index.zfill(cfg.zero_fill)

        if cfg.mini_txtp:
            outname = filename_path
            if index:
                outname += "#" + index

            if cfg.layers and cfg.layers < self.channels:
                for layer in range(0, self.channels, cfg.layers):
                    mask = self._get_stream_mask(layer)
                    self._write(outname + mask, '')
                    total_done += 1
            else:
                self._write(outname, '')
                total_done += 1

        else:
            filename_base = os.path.basename(filename_path)
            pos = filename_base.rfind(".") #remove ext
            if pos > 1:
                filename_base = filename_base[:pos]

            outname = ''
            if cfg.base_name:
                stream_name = self._clean_stream_name()
                internal_filename = stream_name
                if not internal_filename:
                    internal_filename = filename_base

                replaces = {
                    'fn': filename_base,
                    'filename': filename_base,
                    'ss': index,
                    'subsong': index,
                    'in': stream_name,
                    'internal-name': stream_name,
                    'if': internal_filename,
                }

                pattern1 = re.compile(r"<(.+?)>")
                pattern2 = re.compile(r"{(.+?)}")
                txt = cfg.base_name

                # print optional info like "<text__{cmd}__>" only if value in {cmd} exists
                optionals = pattern1.findall(txt)
                for optional in optionals:
                    has_values = False
                    cmds = pattern2.findall(optional)
                    for cmd in cmds:
                        if cmd in replaces and replaces[cmd] is not None:
                            has_values = True
                            break
                    if has_values: #leave text there (cmds will be replaced later)
                        txt = txt.replace('<%s>' % optional, optional, 1)
                    else:
                        txt = txt.replace('<%s>' % optional, '', 1)

                # replace "{cmd}" if cmd exists with its value (non-existent values use '')
                cmds = pattern2.findall(txt)
                for cmd in cmds:
                    if cmd in replaces:
                        value = replaces[cmd]
                        if value is None:
                           value = ''
                        txt = txt.replace('{%s}' % cmd, value, 1)
                outname = "%s" % (txt)

            # no name set, or empty results above            
            if not outname:
                outname = "%s" % (filename_base)
                if index:
                    outname += "_" + index

            line = ''
            if cfg.subdir:
                line += cfg.subdir
            line += filename_clean
            if index:
                line += "#" + index

            if cfg.layers and cfg.layers < self.channels:
                done = 0
                for layer in range(0, self.channels, cfg.layers):
                    sub = chr(ord('a') + done)
                    done += 1
                    mask = self._get_stream_mask(layer)
                    self._write(outname + sub, line + mask)
                    total_done += 1
            else:
                self._write(outname, line)
                total_done += 1
        return total_done

    def has_more_subsongs(self, target_subsong):
        return target_subsong < self.stream_count

#******************************************************************************

class App(object):
    def __init__(self, args):
        self.cfg = args
        self.crc32 = Cr32Helper(args)

    # check CLI in path (can be called, not just file exists)
    def _test_cli(self):
        clis = []
        if self.cfg.cli:
            clis.append(self.cfg.cli)
        else:
            clis.append('vgmstream_cli')
            clis.append('test.exe')

        for cli in clis:
            try:
                with open(os.devnull, 'wb') as DEVNULL: #subprocess.STDOUT #py3 only
                    cmd = "%s" % (cli)
                    subprocess.check_call(cmd, stdout=DEVNULL, stderr=DEVNULL)
                self.cfg.cli = cli
                return True #exists and returns ok
            except subprocess.CalledProcessError as e:
                self.cfg.cli = cli
                return True #exists but returns strerr (ran with no args)
            except Exception as e:
                continue #doesn't exist

        #none found
        return False

    def _make_cmd(self, filename_in, filename_out, target_subsong):
        if self.cfg.test_dupes:
            cmd = "%s -s %s -i -o \"%s\" \"%s\"" % (self.cfg.cli, target_subsong, filename_out, filename_in)
        else:
            cmd = "%s -s %s -m -i -O \"%s\"" % (self.cfg.cli, target_subsong, filename_in)
        return cmd

    def _find_files(self, dir, pattern):
        if os.path.isfile(pattern):
            return [pattern]
        if os.path.isdir(pattern):
            dir = pattern
            pattern = None
    
        files = []
        for root, dirnames, filenames in os.walk(dir):
            for filename in fnmatch.filter(filenames, pattern):
                files.append(os.path.join(root, filename))

            if not self.cfg.recursive:
                break

        return files

    def start(self):
        if not self._test_cli():
            log.error("ERROR: CLI not found")
            return

        filenames_in = []
        for filename in self.cfg.files:
            filenames_in += self._find_files('.', filename)


        rename_map = {}

        total_created = 0
        total_dupes = 0
        total_errors = 0
        for filename_in in filenames_in:
            filename_in_clean = filename_in.replace("\\", "/")
            if filename_in_clean.startswith("./"):
                filename_in_clean = filename_in_clean[2:]

            filename_in_base = os.path.basename(filename_in)

            #skip starting dot for extensionless files
            if filename_in.startswith(".\\"):
                filename_in = filename_in[2:]

            filename_out = ".temp." + filename_in_base + ".wav"
            created = 0
            dupes = 0
            errors = 0
            target_subsong = 1
            while True:
                try:
                    cmd = self._make_cmd(filename_in, filename_out, target_subsong)
                    log.debug("calling: %s", cmd)
                    output_b = subprocess.check_output(cmd, shell=False) #stderr=subprocess.STDOUT
                except subprocess.CalledProcessError as e:
                    log.debug("ignoring CLI error in %s #%s: %s", filename_in, target_subsong, str(e.output))
                    errors += 1
                    break

                if target_subsong == 1:
                    log.debug("processing %s...", filename_in_clean)

                maker = TxtpMaker(self.cfg, output_b, rename_map)

                if not maker.is_ignorable():
                    self.crc32.update(filename_out)

                if not self.crc32.is_last_dupe():
                    created += maker.make(filename_in_base, filename_in_clean)
                else:
                    dupes += 1
                    log.debug("dupe subsong %s", target_subsong)

                if not maker.has_more_subsongs(target_subsong):
                    break
                target_subsong += 1

                if target_subsong % 200 == 0:
                    log.info("%s/%s subsongs... (%s dupes, %s errors)", target_subsong, maker.stream_count, dupes, errors)

            if os.path.exists(filename_out):
                os.remove(filename_out)

            total_created += created
            total_dupes += dupes
            total_errors += errors

        log.info("done! (%s done, %s dupes, %s errors)", total_created, total_dupes, total_errors)


if __name__ == "__main__":
    Cli().start()

    #if len(sys.argv) > 1:
    #    Cli().start()
    #else:
    #    Gui().start()
