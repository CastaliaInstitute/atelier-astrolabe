import type { CapacitorConfig } from '@capacitor/cli';

const config: CapacitorConfig = {
  appId: 'org.castalia.astrolabe.provisioner',
  appName: 'Astrolabe Link',
  webDir: 'dist',
  ios: {
    contentInset: 'always',
  },
};

export default config;
