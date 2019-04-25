
# Issue

Need to restrict the normal users to run only limited set of commands assigned to him/her and all other commands for which normal user have permission to execute by-default, shall not be executed.

E.g: user XYZ can run only gzip and gunzip commands.
Resolution

## Disclaimer : This is just a hack, not recommended for Actual Production Use

The normal user has been given permission to execute some commands which are available in `/bin/` and `/usr/local/bin/`, So to remove those permissions and to restrict the user to run only particular set of commands, following steps shall be useful.

1. Create the restricted shell.

```bash
cp /bin/bash /bin/rbash
```

2. Modify the target user for the shell as restricted shell

While creating user:
```bash
useradd -s /bin/rbash localuser
```

For existing user:
```bash
usermod -s /bin/rbash localuser
```
For more detailed information on this, please check the KBase Article 8349

Then the user localuser is chrooted and can't access the links outside his home directory `/home/localuser`

3. Create a directory under `/home/localuser/`, e.g. programs

```bash
mkdir /home/localuser/programs
```

4. Now if you check, the user localuser can access all commands which he/she has allowed to execute. These commands are taken from the environmental PATH variable which is set in `/home/localuser/.bash_profile`. Modify it as follows.

```bash
# cat /home/localuser/.bash_profile  
# .bash_profile  

# Get the aliases and functions  
if [ -f ~/.bashrc ]; then  
. ~/.bashrc  
fi  
# User specific environment and startup programs  
PATH=$HOME/programs  
export PATH
```

Here the `PATH` variable is set to `~/programs` directory, as `/usr/local/bin` is binded to `/home/username/bin` and /bin is binded to `/home/username/bin` so replacing that.

5. Now after logging with the username `localuser`, user cant run a simple command too. The output will be like this,
Raw

[localuser@example ~]$ ls  
-rbash: ls: command not found  
[localuser@example ~]$ less file1  
-rbash: less: command not found  
[localuser@example ~]$ clear  
-rbash: clear: command not found  
[localuser@example ~]$ date  
-rbash: date: command not found  
[localuser@example ~]$ ping redhat.com  
-rbash: ping: command not found

6. Now create the softlinks of commands which are required for user localuser to execute in the directory `/home/localuser/programs`

```bash
# ln -s /bin/date /home/localuser/programs/  
# ln -s /bin/ls /home/localuser/programs/  
# ll /home/localuser/programs/  
total 8  
lrwxrwxrwx 1 root root 9 Oct 17 15:53 date -> /bin/date  
lrwxrwxrwx 1 root root 7 Oct 17 15:43 ls -> /bin/ls
```

Here examples of date and ls commands has been taken

7. Again login with user `localuser` and try to execute the commands.

```
[localuser@example ~]$ date  
Mon Oct 17 15:55:45 IST 2011  
[localuser@example ~]$ ls  
file1 file10 file2 file3 file4 file5 file6 file7 file8 file9 programs  
[localuser@example ~]$ clear  
-rbash: clear: command not found
```

8. One more step can be added to restrict the user for making any modifications in their `.bash_profile` , as users can change it.

Run the following command to make the user `localuser`'s `.bash_profile` file as immutable so that root/localuser can't modify it until root removes immutable permission from it.

```bash
chattr +i /home/localuser/.bash_profile
```

To remove immutable tag,

```bash
chattr -i /home/localuser/.bash_profile
```

Make file `.bash_profile` as immutable so that user localuser can't change the environmental paths.


## Refrence
- https://access.redhat.com/solutions/65822