# Export CyberDeck vault before reflash

Do this **while the old CyberDeck firmware is still running** and on the LAN.

```bash
# Replace with cyberdeck.local or the device IP
curl -s http://cyberdeck.local/api/vault | tee cyberdeck_vault_backup.json
curl -s http://cyberdeck.local/api/wifi | tee cyberdeck_wifi_status.json
curl -s http://cyberdeck.local/api/status | tee cyberdeck_status.json
```

After flashing `firmware/cyberdeck` from this monorepo, restore vault entries via the CyberDeck UI or a restore script (TODO: `restore_cyberdeck_vault.py`).
