# backupfs - A simple custom filesystem to create backups

I bought block storage to create backups, and I need a system to upload backups
cleanly and securely. Ideally I'd use mostly existing systems and only add a
little bit of code on top. Ideally file names get managed server side so that
the client can't screw anything up.

I thought about using `scp`, which copies files over ssh, but I'd have to
specify the file path on the server to copy to, which would definitely lead to
mistakes. I could have a single path that clients copy to and a server-side
script which moves files copied there to some other place, but then a backup
wouldn't be an atomic operation. I'd have to copy, _then_ move, rather than just
copying. If I successfully copy but my computer dies before the move, I'm
screwed.

In comes backupfs. You just mount the filesystem, copy to a single file
location, and it redirects that data to a new location.
