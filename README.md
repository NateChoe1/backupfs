# backupfs - A fine-grained backup system

I don't trust cloud storage companies with my backups. Not that I have anything
against them specifically, I just don't want to add an extra point of failure to
my system architecture. Ideally, I'd encrypt all of my data client-side and send
a big blob over to some server that will never be decrypted until the heat death
of the universe.

Unfortunately, most backup systems aren't built this way. Systems like `rsync`
require the server and client to have a plaintext copy of the same data so that
they only have to update the altered files at each sync.

It wouldn't be too difficult to set up your own system like this, though. You'd
just have to add some script like this into a cron job:

```bash
tar -czO /path/to/data | gpg -o - -c --batch --passphrase "encryption-password" | upload-blob
```

Unfortunately, that final `upload-blob` command is surprisingly difficult to
implement securely. Ideally, the client would have a fine-grained privilege that
only allows them to upload blobs, and nothing more. If an attacker breaks into
the client, the attacker should not be able to alter, delete, or even access
existing backups.

This repo is my solution. This custom filesystem creates a single file. Any
write calls to this file are redirected to some other path on the system. The
client has some fine-grained privilege that only allows them to write to this
one file, they can't access the real path in which backups are stored. This
system makes the client-side code a lot simpler, reduces client privileges,
prevents buggy client code from deleting existing backups, and is just cool.
