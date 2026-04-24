<template>
  <div>
    <!-- The configuration landing page just wraps other elements which are shown when the configuration data loads -->
    <TabContainer :tabs="tabs" v-if="configStore.loaded">
      <template v-slot:header>
        {{ $t('device_config.configure_tiltbridge_header') }}
      </template>
    </TabContainer>
    <div v-else>
      {{ $t('device_config.config_failed_error') }}
    </div>
  </div>

</template>

<script setup>
import { onMounted, onBeforeUnmount } from "vue";
import { useConfigStore } from "@/stores/ConfigStore";
import { useLoading } from 'vue-loading-overlay'
import TabContainer from "@/components/sitewide/TabContainer.vue";
import { i18n } from "@/main.js";

const $loading = useLoading({
  // options
});

const tabs = [
  { name: i18n.global.t('navigation.general_settings'), route_name: 'TiltBridgeConfig' },
];

let intervalObject = null;
const configStore = useConfigStore();

onMounted(() => {
  // Retrieve initial data
  let loader = $loading.show({});
  configStore.getConfig().finally(() => {
    loader.hide();
  });

  // Set up periodic refreshes
  intervalObject = window.setInterval(() => {
    configStore.getConfig();
  }, 1000*60*30)  // Re-pull once every 30 minutes just in case someone leaves a window open or something
});

onBeforeUnmount(() => {
  clearInterval(intervalObject);
});

</script>

<style scoped>

</style>