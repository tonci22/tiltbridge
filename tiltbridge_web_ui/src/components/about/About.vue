<template>
  <div>
    <UptimeStatsPanel />
    <WifiLinkPanel />
    <SenderHealthPanel />
    <ResetActionsPanel />
  </div>
</template>

<script setup>
import UptimeStatsPanel from "@/components/about/UptimeStatsPanel.vue";
import WifiLinkPanel from "@/components/about/WifiLinkPanel.vue";
import SenderHealthPanel from "@/components/about/SenderHealthPanel.vue";
import ResetActionsPanel from "@/components/about/ResetActionsPanel.vue";
</script>

<style scoped>

</style>
