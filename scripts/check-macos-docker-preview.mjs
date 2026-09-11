import { spawnSync } from 'node:child_process'
import { join } from 'node:path'
import { checkMac, compose, root } from './check-build-contracts.mjs'

const file = join(root, 'docker-compose.x86.macos.yml')
checkMac(compose(file))
checkMac(compose(file, { COSMO_X86_WEB_PORT: '8090', COSMO_X86_BUILD_JOBS: '3' }), '3', '8090')
const result = spawnSync('python3', ['-m', 'unittest', 'discover', '-s', 'test', '-p', 'test_macos_launcher.py'], { cwd: root, stdio: 'inherit' })
if (result.status !== 0) process.exit(result.status || 1)
console.log('Mac effective configuration and unchanged launcher contracts passed')
