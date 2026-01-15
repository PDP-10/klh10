# Install a PDP-10/klh10 DECSYSTEM-20 Running TOPS-20 Panda on an Apple Macintosh or Raspberry Pi with TCP/IP Service Between Guest and Host  
David Todd, HDTodd@gmail.com, 2025.11.11; revised 2026.01.14 

These are step-by-step instructions to install an emulator for the DECSYSTEM-2060 computer onto an Apple Mac or a Raspberry Pi running Trixie, install and configure its TOPS-20 operating system, and establish TCP/IP Telnet and FTP communications between the DEC20 and its host and other computers on your LAN.  The information and tools to do this are readily available from public sources: this guide simply integrates that information into one document and clarifies the setup of the network connection.  Check `klh10/doc` for the original klh10 documentation.

Following this guide, you will:

* Install a DECSYSTEM-2060 emulator onto a Raspberry Pi or Apple Macintosh
* Install a TOPS-20 operating system for that emulated computer
* Install software, documentation, and magtape libraries to support that DECSYSTEM-20
* Establish TCP/IP connectivity between the DECSYSTEM-2060 and its host.

The whole process will likely take 60-90 minutes of work, alternating between your host and guest systems.

## Tools Needed

1. As host, an Apple Macintosh or a Raspberry Pi (preferably something between a Pi-3 and a Pi-5, but it runs fine on a Pi Zero 2 W) with appropriate power supply, keyboard, mouse/trackpad, display, and at least 1GB of available storage capacity. 
1. An Internet connection for that host.

## Working Assumptions

This guide provides links to software and documentation libraries and details on the installation but does not attempt to provide guidance on the use or management of TOPS-20.  The various reference manuals provide that information.

Nor does this guide provide any information on managing the Raspberry Pi: it assumes that you're comfortable working in the Linux environment and using the CLI commands and tools.

This installation is successful on Apple Mac OSX Sierra and Tahoe and on Raspberry Pi (Debian) Trixie.  While this installation would likely work on earlier versions of the Raspberry Pi operating system, this guide assumes that you have a fresh installation of RPi Trixie.  Creation of this guide was motivated by an installation of the DEC20 on an earlier, migrated version of RPi OS in which the TCP/IP connection simply could not be made to function.  The problem was resolved with a clean install of the DEC20 onto a Raspberry Pi running a clean install on Trixie.

## Setting Up

The setup processes for the Pi and the Mac differ, but once the tools are installed the processes are the same.  Use the appropriate following subsection (Pi or Mac) to prepare your host computer, then resume with gathering DECSYSTEM-20 tools.

> [!TIP]
> Assignment of IP addresses for your guest DEC20 is tricky if you have two network interfaces.  You'll want to assign in `klt20.ini` the interface to the _secondary_ interface of your host (e.g., `wlan0` if your host IP address is associated with `eth0` in your DNS tables or `/etc/hosts` file).

### Pi: Preparing to Compile and Host the DEC20

1. Install Trixie on your Raspberry Pi: easiest to use the [Raspberry Pi Installer](https://www.raspberrypi.com/software/) and instructions
2. Make sure your software is up to date:
	* $sudo apt-get update
	* $sudo apt-get full-upgrade
	* [$sudo reboot if updates were performed]
3. Choose a hostname for the emulated DEC20 you'll be creating.  I chose 'dec20'.
4. Choose a unique IP address for your emulated DEC20.  My network is 192.168.1.x with a router at 192.168.1.1; I chose 192.168.1.60 for 'dec20' and made sure it didn't conflict with any other host on my net.
5. If you're using a local DNS system to serve your LAN (on your router, for example), enter your DEC20's hostname and IP address into that DNS table and apply it.

I use my router's DHCP service to assign specific IP addresses to each of my connected hosts, and each of my hosts has a `/etc/hosts` file that lists all hosts and their IP addresses.  You'll likely need to modify some networking parameters, perhaps just adding your DEC20's hostname and IP address to `/etc/hosts` on the Pi that you're installing your DEC20 onto.

Don't edit the network connections on your Pi from the initial installation except to modify the `/etc/hosts` file to add your DEC20 host IP address. The Pi networking environment should have devices lo, eth0, and wlan0.  Ensure that after booting the Pi, Internet addresses have been assigned to eth0 (if connected) and wlan0 and that both network connections are functioning.  For example,
<sub>
```
	hdtodd@pi-5:~ $ip -c address 
	 
	1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue state UNKNOWN group default qlen 1000
	    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
	    inet 127.0.0.1/8 scope host lo
	       valid_lft forever preferred_lft forever
	    inet6 ::1/128 scope host noprefixroute 
	       valid_lft forever preferred_lft forever
	2: eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc fq_codel state UP group default qlen 1000
	    link/ether 2c:cf:67:ad:c9:b9 brd ff:ff:ff:ff:ff:ff
	    inet 192.168.1.88/24 brd 192.168.1.255 scope global dynamic noprefixroute eth0
	       valid_lft 75238sec preferred_lft 75238sec
	    inet6 fe80::c953:95e9:baf2:6a3a/64 scope link noprefixroute 
	       valid_lft forever preferred_lft forever
	3: wlan0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc fq_codel state UP group default qlen 1000
	    link/ether 2c:cf:67:ad:c9:bb brd ff:ff:ff:ff:ff:ff
	    inet 192.168.1.89/24 brd 192.168.1.255 scope global dynamic noprefixroute wlan0
	       valid_lft 75243sec preferred_lft 75243sec
	    inet6 fe80::5018:b779:1c7a:15a3/64 scope link noprefixroute 
	       valid_lft forever preferred_lft forever
```
</sub>

TOPS-20 only supports Telnet and FTP, which aren't up to modern security standards and don't come installed on the Pi.  We'll need to install those tools on the Pi manually in order to connect to the DEC20 when we have it running.  We'll install both Telnet and FTP on the Pi so that it can connect _into_ the hosted DEC20, and we'll install telnetd and vsFTPd on the Pi so that the DEC20 can connect _out_ to the Pi:

* $sudo apt-get install telnet (this is the client)
* $sudo apt-get install ftp (this is the client)
* $sudo apt-get install telnetd (to install telnet service)
* $sudo apt-get install vsftpd (to install secure ftp service)
* $sudo update-inetd --enable telnet (to enable telnetd service); also edit `/etc/vsftpd.conf` to uncomment `write_enable=YES` to allow transfers to the host Pi, then `sudo systemctl restart vsftpd`.

Telnetd and vsFTPd are added as a system services and so start automatically.  Confirm that the Pi is offering telnet and FTP service by, for example, issuing the command to telnet and FTP to itself (where `<hostname>` is the name of your Pi in the `/etc/hosts` table).  For example:

* $ftp \<hostname\>

which should present the vsFTPd prompt:
```
	Connected to <hostname>
	220 (vsFTPd 3.0.5)
	Name (<hostname>:<username>):
```

And, finally, we need to install the pcap development library in order for the DEC20 to be able to use the Pi's network:

* $sudo apt-get install libpcap-dev

### Mac: Preparing to Compile and Host the DEC20

As for the Pi, you'll need to pick a hostname and IP address for your guest DEC20 and add them to your Mac's `/etc/hosts` table.  If you're using a local DNS system to serve your LAN (on your router, for example), enter your DEC20's hostname and IP address into that DNS table and apply it.

The Mac Xcode tools *may* compile the DEC20 code, but but my attempt failed so I used [MacPorts](https://www.macports.org/) tools.  `brew` would probably work as well, if you're already using it, and you'd need to install the same set of tools in `brew` as I did with MacPorts. [I'm not suggesting that you install the telnetd or ftpd _server_ components; just the clients].

* $sudo port selfupdate
* $sudo port upgrade outdated
* $sudo port install gcc
* $sudo port install inetutils (or install from `brew` or download inetutils-2.4.tar.gz from https://ftp.gnu.org/gnu/inetutils/ and compile and install) to get telnet and ftp for the Mac.
 
The Apple Mac comes with pcap installed, so we don't need to do anything more to prepare for networking support.

## Gather the DEC20 Materials You'll Need

We'll need the emulator and associated software, a virtual disk with the TOPS-20 OS, manuals, and magtape images to support the DEC20 installation, so we'll start by collecting those.

Several emulators are available for the PDP-10 processors, but we're going to use Ken Harrenstien's 'klh10' system, and from that system we'll use just the code that emulates the KL10-B processor that was the heart of the DECSYSTEM-2060.  The 'klh10' code has been maintained on a Github site, and that code compiles cleanly on Raspberry Pi Trixie  and OSX MacPorts gcc (as of this writing).

Start by creating a directory that will hold the _source_ materials for the system and its documentation.  For example, in your home directory, you might:

* $mkdir DEC20Sources; cd DEC20Sources

to create and connect into that directory.  We'll work there to build the emulator and associated software, documentation, and magtape libraries.

Working in that directory, clone Harrenstien's klh10 code:

* $git clone http://github.com/pdp-10/klh10

The klh10 code is just the emulator.  We still need a systems disk and the TOPS-20 software.  

We'll take the easy route and start with Mark Crispin's Panda package that has the systems disk already created, loaded with applications, and (mostly) configured for use.  That package is located on the Trailing-Edge site,  [Panda TOPS-20 distribution](https://panda.trailing-edge.com/).

So, again while working in your DEC20Sources directory, issue:

* $wget https://panda.trailing-edge.com/panda-dist.tar.gz (or `curl` if `wget` not installed)
* $tar -xzf panda-dist.tar.gz

which will create the directory `panda-dist` in your DEC20Sources directory.

You may eventually want to install software from some of the other magtapes in the library (see below), and it's convenient to be able to read those tapes on the OSX/Linux side as well as read/create them on the TOPS-20 side.  The `read20` program does that, but MRC's version is outdated.  We'll use an updated version from another github repository:

* $git clone https://github.com/brouhaha/tapeutils

which creates the `tapeutils` directory in your DEC20Sources directory and loads the program source code.

You'll want to keep a set of TOPS-20 manuals handy for reference, so create a directory for them in DEC20Sources:

* $mkdir Manuals

The last general release of TOPS-20 was version 7.x, but earlier reference materials -- particularly for Macro, FORTRAN, etc. -- didn't change much.  So reference materials for TOPS v6 or V7 are probably most relevant.  

In your web browser, go to [Zane Healy's site](https://www.avanthar.com/healyzh/decemulation/TOPS-20_notebooks.html) and select and download into your "Manuals" directory the reference materials you think you might need.  That set should at least include:

* [Users Guide](http://www.avanthar.com/healyzh/tops20_7/USERS.pdf)
* [User Utilities](http://www.avanthar.com/healyzh/tops20_7/USER_UTILS.pdf)
* [Commands Reference Manual](http://www.avanthar.com/healyzh/tops20_7/COMMANDS.pdf)
* [Systems Managers Guide](http://www.avanthar.com/healyzh/tops20_7/MGR_GUIDE.pdf)
* [Operator's Guide](http://www.avanthar.com/healyzh/tops20_7/OP_GUIDE.pdf)
* [Operator's Command Language Ref Manual](http://www.avanthar.com/healyzh/tops20_7/OPRCOM.HTML)

[You can use `wget` with each of those links, in turn, to download that set.]

Finally, there is a substantial library of magnetic tapes with system software components that can be installed on your DEC20 system.  Most of them are already installed on MRC's Panda distribution, but you may want some of the others.  It's handy to keep them all in one place, so, again start in your "DEC20Sources" directory and:

* $mkdir Tapes; cd Tapes

In your web browser, go to [the Trailing Edge PDP-10 site](https://pdp-10.trailing-edge.com/) and select a few of the tapes for components you think might be useful. In particular, consider downloading the tapes identified as "T20 DOCUMENTATION UPD TAPE 1990": they contain, in text format, the software notebooks that describe system installation and management.  It's easiest to right-click on the "tape image" link associated with a desired tape, copy that link, then in a terminal window connected to the "Tapes" directory, type "wget " and then paste the link URL for the tape.  You'll download a .bz2 file.  Use:

* $bunzip2 -d \<filename\>

to unzip the files.  You could restore the tape contents to your TOPS-20 systems disk with the DUMPER utility, but you'll most likely find it more useful to be able to list (-t) or extract (-x) the tape files on your Raspberry Pi with the Linux `read20` utility we'll build and install with the "klh10" package.

There is also a substantial collection of user-contributed software in the DECUS library, and the Trailing Edge site contains the library of 11 DECUS DEC20 tapes.   [This](https://pdp-10.trailing-edge.com/decuslib20-07/01/decus/20-0000/20-library-distribution-notes.mem.html) is an (incomplete) catalog of holdings 1-149 out of 192 items, some of which might be of interest.

## Compiling and Installing the System

### Compiling and Installing klh10
To compile the klh10 emulator, we'll follow a subset of the instructions in `~/DEC20sources/klh10/doc/install.txt`.  When we finish, we'll install the emulator, associated support files, and virtual (Panda) system disk in a production directory.  We'll run that emulator as root, so create that directory,

* $sudo mkdir /DEC20

(or choose some other appropriate directory name and location -- OSX is fussy).  

Though you'll run the emulator as root, you'll find it convenient for maintenance tasks to have ownership of that directory, so

* $sudo chown  \<your account name\>:\<your group name\> /DEC20 (or whatever you chose as directory name/location)

e.g., `sudo chown hdtodd:hdtodd /DEC20` .

The klh10 system uses KLH10_HOME to point to that directory, so:

* $KLH10_HOME=/DEC20; export KLH10_HOME

to set that up (**and add that to ~/.profile to avoid having to do it in the future**).  Now we can compile and install the DEC20 emulator:
```
$cd DEC20sources/klh10
$mkdir tmp; cd tmp
$../configure
$make -C bld-kl
$make -C bld-kl install
```
You've now created and installed the klh10 emulator:
<sub>
```
	$ ls /DEC20
	dpni20  dprpxx  dptm03  enaddr  flushed  kn10-kl  tapedd  udlconv  uexbconv  vdkfmt  wfconv  wxtest
```
</sub>

> [!NOTE]
> If you have trouble compiling the code, `cd DEC20Sources/klh10/tmp` then `../autogen.sh --bindir=$KLH10_HOME` to re-generate the configuration files, `make clean`, and then `make -C bld-kl` again.  You may need to install `autoconf` if it is not already installed: `$sudo port install autoconf` on Mac or `$sudo apt-get install autoconf` on Pi.

We'll install the `read20` program in your DEC20 production directory, too:

* $cd ../../tapeutils
* $make
* $cp read20 /DEC20

The other tapeutilities are likely not useful, so we won't clutter /DEC20 with them.  But if you unziped your tapes in the `Tapes` subdirectory, you can now see what the tapes contain with, for example, `./read20 -t -f ../Tapes/BB-PBQUA-BM_1990.tap` .

### Installing the TOPS-20 Systems Disk

We have the klh10 emulator installed but now need the TOPS-20 operating system disk.  We'll just use the Panda drive that MRC has left for us.  Most of the files in the `panda-dist` directory are programs compiled for other architectures and so aren't useful here. But the RP07 virtual disk and the .ini and boot files will work just fine on our newly-compiled klh10, so we'll move those files to our /DEC20 directory:

```
	$cd DEC20Sources/panda-dist
	$mv boot.sav /DEC20
	$mv dfkfb.ini /DEC20
	$mv klt20* /DEC20
	$mv mtboot.sav /DEC20
	$mv README* /DEC20
	$mv RH20.RP07.1 /DEC20
	$sudo chown root:root /DEC20/dpni20
	$sudo chmod +s /DEC20/dpni20
```

After all that, you should have:
<sub>
```
	$ls /DEC20
	boot.sav   dpni20  dptm03  flushed    kn10-kl     read20  README-MULTIUNIT-RP07  tapedd   uexbconv  wfconv
	dfkfb.ini  dprpxx  enaddr  klt20.ini  mtboot.sav  README  RH20.RP07.1            udlconv  vdkfmt    wxtest
```
</sub>

The RP07 disk is a 500MB drive and should be large enough for casual work.  If you need additional storage, you can add more RP07's and MOUNT them under TOPS-20 later.  See Harenstien's installation instructions `klh10/doc/install.txt` to add other devices.

### Configuring klh10

We have a change to make to the klh10 configuration file, `/DEC20/klt20.ini`, in order for it to establish the network connection. The Pi has network interfaces `lo` (loopback), `eth0` (ethernet), and `wlan0` (WiFi).  The Mac has a long list of network interfaces.  We need to know which interfaces are active, so do an `ifconfig` command.  

On the Pi, you should have IPv4 addresses assigned to `eth0` or `wlan0` or both.  Pick one with an IPv4 address to use in the `devdef` parameter setting below.  

On the Mac, look particularly for IPv4 addresses being assigned to either `en0` or `en1` or both.  Pick one with an IPv4 address to be used below.

Edit your /DEC20/klt20.ini file to have the `devdef` for your network interface use the device you identified above (likely `eth0` or `wlan0` on Pi; `en0` or `en1` on Mac) to replace the "xxx":

```
	devdef ni0 564 ni20 dedic=true ifmeth=pcap ifc=xxx
```

We still have to make some changes in the TOPS-20 internal configuration files in order to make the network connection, but we're finished with the configuration work on the Pi or Mac for now. 

### Testing the DEC20 System

We can now boot your DECSYSTEM-20!  

Both Harrenstien's `klh10/doc/install.txt` and MRC's `/DEC20/README` give instructions on how to boot the DEC20.  If you have trouble with the steps below, refer to those files for more information.  The process below is modeled on those guides.

1. `$cd /DEC20`
1. `$sudo ./klt20`
1. At the prompt
			KLH10# ; Ready to GO</br>
			KLH10# [EOF on klt20.ini]</br>
			KLH10#</br>
type `GO`
1. In response to</br>
			BOOT\> </br>
just press the RETURN button.
1. In response to </br>
			Why reload?</br>
type `opr`
1. In response to </br>
			Run CHECKD?</br>
type `no`

The system will boot up and start its various internal processes.  If you scroll back through your screen, you should see something like this:
<sub>
```
KLH10 2.0l (MyKL) built Jan  7 2026 05:27:55
    Copyright ? 2002 Kenneth L. Harrenstien -- All Rights Reserved.
This program comes "AS IS" with ABSOLUTELY NO WARRANTY.

Compiled for unknown-linux-gnu on aarch64 with word model USEINT
Emulated config:
	 CPU: KL10-extend   SYS: T20   Pager: KL  APRID: 3600
	 Memory: 8192 pages of 512 words  (SHARED)
	 Time interval: INTRP   Base: OSGET
	 Interval default: 60Hz
	 Internal clock: OSINT
	 Other: MCA25 CIRC JPC DEBUG PCCACHE CTYINT EVHINT
	 Devices: DTE RH20 RPXX(DP) TM03(DP) NI20(DP)
[MEM: Allocating 8192 pages shared memory, clearing...done]

...
```
</sub>

That's klh10 initializing its microcode and memory and loading its interface devices.  You'll see some error messages in this first boot, but you can ignore them for now.  The IP address comes from a DEC20 configuration file, so the network won't work yet, but the system is in operation.

When those have finished, press CONTROL-C (`^C`) and TOPS-20 will respond with a command-line prompt, `@ `, and you can log in with operator credentials:

* @login operator dec-20

*where the password "dec-20", will not echo on the display*.

You're now logged into the DEC20 as "operator".  

### Getting Comfortable

Note that TOPS-20 commands and guide words are **not** case sensitive:  `VdIr`, `VDIR`, and `vdir` all print a verbose directory. And control characters are generally represented with an `^` character before them, but type them by holding down the "CONTROL" or "CTRL" button and typing the character.  So `^E` means hold down CONTROL and type `E`.

Many commands offer guidewords and command word completion, and most accept abbreviated commands.  "?" lists the alternatives at that point in a command, and [ESC key] offers command completion. You'll see that below, for example: "$wor ?" shows what you can enter as allowed sizes for the account's working storage, and "$$wor inf[ESC]" completed to "infINITY".  Using those keys can help guide you through completing most commands.  

Following MRC's advice, create a personal account for yourself and give it access to privileges.  Log in as OPERATOR, and then:
<sub>
```
	@term vt102
	@enable
	$^Ecreate ps:<your username>  [that's a CONTROL-E key press to start]
	[New]
	$$wor infINITY
	$$perm infINITY
	$$maxIMUM-SUBDIRECTORIES (ALLOWED) 1000
	$$wheel
	$$oper
	$$passWORD <your password here>
	$$ [press RETURN to execute the command]
	3-Nov-2025 12:24:56 ACJ: Create directory PS:<your username> with privileges by job 9,
	user OPERATOR, program CREATE, TTY5
	$
```
</sub>

Now `@logout[RETURN]`, press `^C` [CONTROL-C] to get an `@ ` prompt, and log in as yourself with your password.  Type `@ ENA` to make sure you can access operator privileges.  

The Panda system offers several text editors: EMACS, EDIT, and TECO.  If you use EDIT, remember to "eu" to exit ("exit and unnumber").  If you're familiar with EMACS, you'll find the version on DEC20 to be quite primitive, but it works.  The arrow keys don't function (use ^B, ^F, ^P, ^N) but it's usable.

You'll find it helpful to at least set your terminal characteristics at login time.  On the Pi, using its terminal program, type "VT102" seems to work reasonably well for working with TOPS-20).

Using the editor of your choice (EMACS in my case):

```
	@term vt102
	@emacs login.cmd
	term vt102
	term width 80
	term len 24
	^x^s^x^c  [control characters to save file and exit]
	@take login.cmd
```

On your next login, that file will be executed automatically.

You've now seen the login "beware" message, so we can eliminate it:
```
	@enable
	$conn ps:<system>
	$emacs beware-message.txt
	[delete text]
	^x^s^x^c
	$
```

Of course, you can replace the existing text there with another message if you'd prefer.

The Panda distribution includes some Linux-like programs that _generally_ work as expected, such as `ls`, `grep`, `mv`, `mkdir`, etc., with allowance for the different DEC20 syntax (e.g., `ps:<mydir.subdir>` for directory formats).  You can see a list of those Linux-like programs by typing `@ ls unix-sys:`.  If you're a regular Linux user, you'll likely find yourself surprised that when you accidently type `ls` you get a directory listing: that set of tools is why, since they are not native TOPS-20 commands.

### Configuring the DEC20 for Internet

Back on your DEC20 terminal window, enable privileges with `@ena`.

Recall the IP address and hostname you have given your DEC20 in your Pi or Mac's `/etc/hosts` file.  In your DEC20 terminal session, edit the file `<system>internet.address`, and change the IP address for "ni0" to yours.  For example,
```
IPNI#0,192.168.1.60,PACKET-SIZE:1500,LOGICAL-HOST-MASK:255.0.0.0,DEFAULT,PREFERRED
```

Edit the file `<system>internet.gateways` to provide the gateway for your local area network, e.g.,
```
PRIME 192.168.1.1
```

We'll do some other DEC20 housekeeping before we reboot and try the network.

You have to "enable" (`@ena`) your privileges to edit the TOPS-20 system files.  

Following MRC's guidance in his "README", you'll need to edit these files to configure your DEC20 and set it up for Internet service.  MRC: "The format of most of these files should be obvious, but if you aren't sure please ask questions:"
<sub>
```
	SYSTEM:7-1-CONFIG.CMD      to set your timezone [NYC is +5]
	SYSTEM:HOSTS.TXT           to define your local host name, gateway, and network
	SYSTEM:INTERNET.ADDRESS    to define your IP address
	SYSTEM:INTERNET.GATEWAYS   to define your IP gateway
	SYSTEM:MONNAM.TXT          to define your system name
	DOMAIN:RESOLV.CONFIG       to define your DNS servers, your default
	                            domain (replacing MYDOMAIN.COM) and any
	                            users in addition to OPERATOR who can
	                            send control messages to the resolver.
	SYSTEM:SYSJB1.RUN			move the "RUN:ORION" line to the top of the file
```
</sub>

For example, with my host Pi ("pi-0", 192.168.1.78) and guest DEC20 ("dec20", 192.168.1.60), with gateway 192.168.1.1, the beginning of my HOSTS.TXT file looks like this:
```
NET     : 192.168.1.0  : Todds  :
GATEWAY : 192.168.1.1  : Router :        :        : IP/GW,TCP/TELNET              :
HOST    : 192.168.1.60 : dec20  : KLH10  : TOPS20 : TCP/TELNET,TCP/FTP,TCP/SMTP   :
HOST    : 192.168.1.78 : pi-0   : ARM64  : Linux  : TCP/TELNET, TCP/FTP, TCP/SMTP :
```
You can edit through those other entries and delete them as those hosts no longer seem to work (20 years later!), but you're done for now.

Several other changes are needed to enable the mail system to run smoothly.

First, create a directory for MX (mail handler):
```
@ena
$build ps:<mx>
[New]
$$wor 1000
$$perm 1000
$$max 100
$$ [RETURN executes the commands]
$
```
Then establish additional logical definitions MX uses by adding these to the end of the file in `<system>7-1-CONFIG.CMD`:
```
DEFINE RUNMX: SYS:MX.EXE
DEFINE UPS: PS:<MX>
```

And finally, edit `<system>sysjb1.run` to add this line at the end (you can use [CNTL-J] or [RETURN] as the character to separate fields):
```
JOB 6 "LOGIN OPERATOR^JENABLE^JMX^J"
```

### Shutting Down

You need to have ENAbled operator privileges in order to shut the DEC20 down:

1. @ena
1. $^Ecease now [again, CONTROL-E keypress to start]
2. [confirm with a carriage return]
3. after "SHUTDOWN COMPLETE", type CONTROL-\ (hold down CONTROL and press the backslash key) to exit to the KLH10 command prompt
4. type "quit"
5. after CONFIRM, press "Y" and then the RETURN key, you'll exit back to the Linux command prompt.

## Testing TCP/IP Connectivity

Reboot your DEC20 system.  Type `^C` to get the command prompt. Log in as yourself.

> [!WARNING]
> If you are on a single-network-interface host, you may find that you can only connect to the guest DEC20 from _another_ computer.

If you're running on a Pi as host, type:

* @ftp \<your Pi host's name\>

There may be a delay of a minute or two, but then you should see:

```
	< (vsFTPd 3.0.5)
	Setting default transfer type to text.
	FTP>
```
showing that you can log in to your Pi account and transfer files.

From a different terminal session on your Mac or Pi, type:

* $telnet dec20 (or whatever you named your DEC20 IP hostname)

Telnet will try to make the connection:
```
	Trying 192.168.1.60...
	Connected to dec20.
	Escape character is '^]'.
	
	HDTsKL, PANDA TOPS-20 Monitor 7.1(21733)-4
	@ 
```

Log in, then log out, and then, again into your second Pi terminal window type:
* $ftp dec20
and you should see the ftp prompt from your DEC20:

```
	Connected to dec20.
	220 DEC20 FTP Server Process 5Z(40)-7 at Wed 5-Nov-2025 16:14-EST
	Name (dec20:hdtodd):
```

You now have TCP/IP connectivity between your Pi and your hosted DEC20.  The FTP syntax is archaic, but with "?" you can work through connecting and transferring files.

If you want to telnet or ftp _out_ of the DEC20 to an IP address rather than node name, you need to include the IP address in "[]":
```
@telnet [192.168.1.78]
```

The `finger` program informs you who is on various systems.  Try `finger @<your DEC20's name>` and you should see yourself.  Try `finger @mim.softjar.se` to confirm that your `resolv.config` has both local (e.g., 192.168.1.1 for me) and global (e.g., 8.8.8.8 for me) DNS servers.

### Cleaning Up

During the boot process, you might have noticed:
<sub>
```
	SJ  0: 12:12:29          -- Structure Status Change Detected --
	SJ  0:                 Structure state for structure TOPS-20 is incorrect
	SJ  0:                   EXCLUSIVE/SHARED attribute set incorrectly
	SJ  0:                 Status of structure TOPS-20: is set:
	SJ  0:                 Domestic, Unregulated, Shared, Available, Dumpable
```
</sub>
That seems to reflect a disk structure status of MRC's TOPS-20 system disk, which appears to have the SHARED attribute set.  While the system resets it as it boots, you can fix it permanently with the following commands to `opr`, the operator's management program:

```
	@ena
	$opr
	OPR> set structure tops20: exclusive
	OPR> exit
	$ 
```
We also need to clean up any lost pages on the DEC20's virtual drive.

1. Reboot the system  **BUT ...**
1. At the "WHY RELOAD", type "SA" (we're going to run StandAlone to start)
1. At "RUN CHECKD?", type "Y" and let the CHECKD program check the systems disk for errors.  The only errors should be lost pages -- storage pages that had belonged to some file but were orphaned.  It's usually a small number of pages, but let's clean that up.
1. After CHECKD runs, it'll report something like "24 lost pages" and store that information in the file `PS:<OPERATOR>TOPS-20-LOST-PAGES.BIN` and then continue to reboot.
1. After the boot process has completed, log in and:

```
	@ena
	$CHECKD
	CHECKD>release TOPS-20:TOPS-20-LOST-PAGES.BIN.1 tops20:
	CHECKD>exit
```
You may need to do this occasionally as regular maintenance, particularly if you've had system crashes or inadvertently terminated the klh10 process when you rebooted your Pi.

## Using Magnetic Tape Images

I found it most convenient to move the magtape library to the `/DEC20` production directory on the Pi.  From a Pi (or Mac) terminal window:

* $mv DEC20Sources/Tapes /DEC20

While klh10 supports the use of physical tape devices, you'll likely just use disk files that have been created to mimic the format of magnetic tapes such as those you might have stored in the `Tapes` directory.

TOPS20 uses the 'DUMPER' program to save/restore files and directories.  

> [!caution]
> **By default, the virtual magtape is mounted read-only, and existing files cannot be mounted read/write!**

To mount a tape on a drive, use the `devmount` directive in `/DEC20/klt20.ini`. For example, to access one of the documentation tapes you might have downloaded earlier:

```
	;THIS is how you define a read-only tape mount
	devmount mta0 Tapes/BB-PBQUA-BM_1990.tap
```

That directive would tell klh10 to "mount" the first DEC20 documentation tape on device mta0:.  Once you've booted up and logged in, you would proceed with DUMPER to rewind the "tape" (`REWIND`), list tape contents (`PRINT`) or restore its contents to disk (`RESTORE`). 

To mount a virtual magtape for writing (e.g., for backing up), in `/DEC20/klt20.ini` you'd use:
```
	;THIS is how you define a writeable tape mount
	devmount mta0 Tapes/Temp.tap rw
```

**where the file `Temp.tap` is non-existent.**  klh10 won't let you write over an existing virtual tape file.  `@ assign mta0:` after you've logged in and then run DUMPER.

## Quirks You May Or May Not See

These are some problems I've seen arise while running klh10 with Panda. 

1.  On a host with only one interface, connectivity between host and guest is at best unreliable and generally doesn't seem to work at all.  You can connect to the guest DEC20 from another computer on your network, but not from the host.  And you can't connect connect to the host from another computer.  This appears to be a result of using pcap as a dedicated interface, but using `dedic=false` in `klt20.ini` results in no connection at all.
1.  Using pcap is less than ideal, and a better arrangement might be to have a dedicated interface on the host to provide network connectivity to the guest DEC20.  But using pcap on a host with two interfaces, I *think* I've found that selecting the WiFi rather than ethernet interface functions more reliably, as the host system appears to prefer to use the faster ethernet (and hence less chance of contention or conflict with the guest).
1.  Startup of telnet and ftp connections from the DEC20 can be very delayed, but they work.
1.  During boot-up, I occasionally get a message on the console ("CTY"): `***BUGCHK IPIBLP*** IPNI input buffer list problem  Job: 0`. This appears to be an issue with the pcap interface.  It generally does not interfere with network connectivity, but it is a concern.  If I find a solution to elminate it, I'll update this.
2.  Occasionally, when booting into standalone-mode ("SA") to run `CHECKD` and look for lost pages, the system hangs.  I simply ^\ and quit and retry and it works.  I don't expect to find the cause of that.
3.  Occasionally, `ORION` fails to start.  This is a random occurrence and appears to be a timing issue among some of the startup procedures.  I generally wait until boot-up has finished, then login over the warning messages to ^ECEASE NOW, and reboot.
4.  I originally intended on the Pi host to use a bridge/tap interface for the network.  After significant effort, and help from some very knowledgeable people, I was unable to get the bridge/tap system working and resorted to pcap.  If anyone is able to get the `klt20.ini` `devdef ni ... ifc=tap` working, please explain how and I'll update this.  I'll work on it again after giving it more thought and with a little more experience.  For those who want to pursue this, I believe the issue may be with the `NetworkManager` network management tool that is now the default on the Pi -- I wanted to work within the Raspberry Pi framework, as distributed.  I've tried making the guest interface `managed no` with no success; more experimenting needed there.
5.  The Mac apparently only supports the pcap interface.  Again, if you know an alternative, please let me know and I'll update.

## Unresolved Issues
 
1. I have not been able to stop the klh10 program with CNTL-\\, use `devmount` to assign a different virtual tape to the drive, and continue.  It's not clear from Harrenstien's documentation whether it should be possible.  If you get it to work, please say how.
2. It appears to have been possible to run `/DEC20/klt20` without having root privileges by having set `dpni20` +s and owned by root, but that no longer works.  If you find a solution, please say how.

## Acknowldegement

Though I've run Panda on a number of systems over the last 20-25 years, I could never get the networking component to work reliably (or at all on some systems).  In late 2025, I was determined to fix it on my systems (Mac and Pi), having seen that others had networks running successfully.  I didn't have the networking expertise, but I had persistence -- and people who patiently helped me work through it:

    * Jayjwa
	* Rhialto
    * Scott Lurndal
    * Johnny Billquist
	* Lars Brinkhoff
	* bictorv
    * Rich Alderson
    * Lawrence D’Oliveiro
    * Andy Valencia

If you find this guide helpful, you have them to thank (as I do).  Check the references for some of the discussion threads if you want details.

## References and Resources

1. Zane Healy has a comprehensive catalog of alternative PDP-10 emulators and OS installs with links to the software available here:  [The DEC PDP-10 Emulation Webpage](https://www.avanthar.com/healyzh/decemulation/pdp10emu.html).  </br>**More importantly, this page has a list of, and links to, TOPS-20 documentation and user manuals in a variety of formats as referenced earlier.**
1. A more recent emulator, not documented in Healy's catalog, was developed specifically for the Pi.  Though it runs the ITS OS, it might be of interest.  It is documented on the [Obsolescence](https://obsolescence.wixsite.com/obsolescence/pidp10) site.
1. Trailing Edge is a primary source of Digital Equipment software distribution tapes and tapes containing the software notebooks used to install and manage DECSYSTEM-20 sites: [Trailing Edge PDP-10](https://pdp-10.trailing-edge.com/).  It also contains the DECUS Library of user-contributed DEC20 software.
2. The newsgroup "alt.sys.pdp-10" has a number of knowledgeable participants who may respond to queries.
3. The github site in which the klh10 resides contains "issues" and "discussion" forums with knowledgable participants who may respond to queries.

## Creating Your Own Systems Disk

If you want to create your own systems disk rather than use the Panda RH20, Harrenstien has instructions on how to do that in `~/DEC20Sources/klh10/run/klt20/README`.  He also provided the `.ini` and booting (`.sav`) programs to get you started.  But you'll need the Digital software distribution tapes to get started.  Download these tapes from the Trailing Edge site and follow the instructions Harrenstien provided.

Download at least these TOPS-20 7.0 Distribution from the Trailing Edge site:

1. [TOPS-20 V7.0 Installation BB-H137F-BM](http://pdp-10.trailing-edge.com/tapes/bb-h137f-bm.tap.bz2)
2. [TOPS-20 V7.0 Distribution Tape 1 of 2 BB-H138F-BM](http://pdp-10.trailing-edge.com/tapes/bb-h138f-bm.tap.bz2)
3. [TOPS-20 V7.0 Distribution Tape 2 of 2 BB-LW55A-BM](http://pdp-10.trailing-edge.com/tapes/bb-lw55a-bm.tap.bz2)
4. [TOPS-20 V7.0 Tools BB-M836D-BM](http://pdp-10.trailing-edge.com/tapes/bb-m836d-bm.tap.bz2) 
5. [TOPS-20 V7.0 TSU04 Tape 1 OF 2 BB-PENEA-BM](http://pdp-10.trailing-edge.com/tapes/bb-penea-bm.tap.bz2)
6. [TOPS-20 V7.0 TSU04 Tape 2 OF 2 BB-KL11M-BM](http://pdp-10.trailing-edge.com/tapes/bb-kl11m-bm.tap.bz2)
7. [TCP/IP-20 V4.0 Distribution BB-EV83B-BM](http://pdp-10.trailing-edge.com/tapes/bb-ev83b-bm_longer.tap.bz2)
8. [TOPS-20 V7.0 Monitor Source BB-M780D-SM](http://pdp-10.trailing-edge.com/tapes/bb-m780d-sm.tap.bz2)
9. [TOPS-20 V7.0 EXEC Source BB-GS97B-SM](http://pdp-10.trailing-edge.com/tapes/bb-gs97b-sm.tap.bz2)
10. [TOPS-20 V7.0 #04 MON SRC MOD BB-M080Z-SM](http://pdp-10.trailing-edge.com/tapes/bb-m080z-sm.tap.bz2)
11. [TOPS-20 V7.0 #04 EXEC SRC MOD BB-M081Z-SM](http://pdp-10.trailing-edge.com/tapes/bb-m081z-sm.tap.bz2)

# Wisdom from klh10's Creator

Harrenstien closed his "history.txt" notes with the following.  For those of us who managed DEC-10's and DEC20's that occupied a large room and consumed incredible amounts of power and cooling, this is very poignant.  Particularly when you boot up (the late) MRC's Panda system.

> Final thoughts:
> --------------
> 
> I was going to close with the last paragraph, but realized there's one more thing I want to say.  It's just that, um, well, this will sound silly, but it feels so... weird? ... eerie?  ... just plain literally *mind-blowing* to watch this system boot up and run happily, utterly unaware that it's not on a real machine, or that anything odd happened since the last time it ran, or that its earthly incarnation of a noisy roomful of huge cabinets and washing machines is now entirely self-contained within a small innocuous pizza box holding up my ITS manuals.  Do systems have wathans?  I've gotten a bit more used to it, but every now and then I still sit back, realize once again what the hell is going on, and hold on to something while  the chills pass.  I didn't expect this at all.  A side effect of being imprinted at a tender young age, or something...
> 
> --Ken

