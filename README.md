# Timcat -- TiMOS ssh exec

## Summary

Timcat facilitates easy, simple CLI automation of devices running TiMOS,
Alcatel-Lucent's OS for their _7xxx Service Router_ platform. It allows you to
issue commands (and hookup stdio), to a TiMOS device with the SSHv2 server
enabled.

## Example

The following example uses the default login (admin/admin) to issue commands
from the command line, and from stdin. I simply filled stdin (using echo) with
a TiMOS CLI command to mimic the familiar _uptime_ command.

[code]

      $ timcat -H sr7host -- "show version"

      INFO: connecting to sr7host (***.***.***.***) on port 22
      
      *A:sr7host# show version 
      TiMOS-C-11.0.R6 cpm/hops ALCATEL SR 7750 Copyright (c) 2000-2013 Alcatel-Lucent.
      All rights reserved. All use subject to applicable license agreements.
      Built on Tue Nov 19 15:12:45 PST 2013 by builder in /rel11.0/b1/R6/panos/main

[/code]
    
[code]
      $ timcat -H sr7host -- "show card state"

      INFO: connecting to sr7host (***.***.***.***) on port 22
      *A:sr7host# show system information | match "System Up Time"
      System Up Time         : 9 days, 14:28:15.45 (hr:min:sec)

[/code]

[code]
      $ echo 'show system information | match "System Up Time"' | timcat -H sr7host 
    
      INFO: connecting to sr7host (***.***.***.***) on port 22
      
      *A:sr7host# show version 
      TiMOS-C-11.0.R6 cpm/hops ALCATEL SR 7750 Copyright (c) 2000-2013 Alcatel-Lucent.
      All rights reserved. All use subject to applicable license agreements.
      Built on Tue Nov 19 15:12:45 PST 2013 by builder in /rel11.0/b1/R6/panos/main
      *A:sr7host# show card state 
      
      ===============================================================================
      Card State
      ===============================================================================
      Slot/  Provisioned Type                  Admin Operational   Num   Num Comments
      Id         Equipped Type (if different)  State State         Ports MDA 
      -------------------------------------------------------------------------------
      1      imm5-10gb-xfp                     up    failed              1   
                 iom3-xp
      1/1    imm5-10gb-xp-xfp                  up    provisioned   5         
                 (not equipped)
      2      iom3-xp                           up    failed              2   
                 imm5-10gb-xfp
      2/1    m20-1gb-xp-sfp                    up    provisioned   20        
                 (not equipped)
      2/2    m16-oc3-sfp                       up    provisioned   16        
                 (not equipped)
      3      imm12-10gb-sf+                    up    failed              1   
                 imm8-10gb-xfp
      3/1    imm12-10gb-xp-sf+                 up    provisioned   12        
                 (not equipped)
      4      (not provisioned)                 up    unprovisioned       0   
                 imm12-10gb-sf+
      5      imm5-10gb-xfp                     up    up                  1   
      5/1    imm5-10gb-xp-xfp                  up    up            5         
      A      sfm3-7                            up    up                      Active
      B      sfm3-7                            up    up                      Standby
                 sfm3-12
      ===============================================================================
      *A:sr7host# show system information | match "System Up Time"
      System Up Time         : 9 days, 14:28:15.45 (hr:min:sec)
      *A:sr7host# 
     
[/code]

## Background

I've been working with Alcatel-Lucent's Service Router platform since late
2007\. Much of my work has been focused on automation and management. Before 
the development of NETCONF, the options were: SNMP, Tcl/Expect, or 
Alcatel-Lucent's 5620 SAM.

I believe that traditional CLI interfaces are, the most powerful, simple, and 
flexible way to operate any device.  I was sick of writing one-off scripts in 
TCL to perform simple tasks on a TiMOS node, so I made this program.

