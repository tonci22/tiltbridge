<template>
  <div class="max-w-7xl mx-auto py-6 sm:px-6 lg:px-8">
    <div class="flex-initial md:container">
      <div class="bg-white overflow-hidden sm:rounded-lg sm:shadow">

        <div class="bg-white px-4 py-5 border-b border-gray-200 sm:px-6">
          <h3 class="text-lg leading-6 font-medium text-gray-900">
            {{ $t('cloud_config.mqtt.header') }}
          </h3>
        </div>

        <div class="px-4 sm:px-6 pt-4">
          <SendTargetErrorMsg target-key="mqtt" />
        </div>

        <div class="bg-white px-4 py-5 border-b border-gray-200 sm:px-6">
          <h4 class="text-lg leading-6 font-medium text-gray-900">
            {{ $t('cloud_config.mqtt.about_header') }}
          </h4>
          <p class="mt-2">
            {{ $t('cloud_config.mqtt.about_text_1') }}
          </p>
          <p class="mt-2">
            {{ $t('cloud_config.mqtt.about_text_hast') }}
          </p>


          <p class="mt-2">
            {{  $t('cloud_config.mqtt.about_text_payload_header') }}:
          </p>
          <p class="mt-1 pl-2">
            <span>{{  $t('cloud_config.mqtt.about_text_topic') }}:</span><br />
            <span>topic/tilt_color</span><br />
          </p>
          <p class="mt-0.5 pl-2">

            <span>{{  $t('cloud_config.mqtt.about_text_payload') }}:</span><br />
            <span>{"Color":"color","SG":"#.####","Temp":"##.#","fermunits":"SG","tempunits":"F","timeStamp":##########}</span>
          </p>
        </div>

        <form @submit.prevent="submitForm">
          <div class="px-4 py-5">

            <TextField v-model="mqttBrokerHost" placeholder="e.g., 192.168.1.200">
              <template #FieldName>{{ $t('cloud_config.mqtt.broker_address') }}</template>
              <template #FieldDescription>{{ $t('cloud_config.mqtt.broker_address_desc') }}</template>
            </TextField>

            <TextField v-model="mqttBrokerPort" placeholder="1883">
              <template #FieldName>{{ $t('cloud_config.mqtt.broker_port') }}</template>
              <template #FieldDescription>{{ $t('cloud_config.mqtt.broker_port_desc') }}</template>
            </TextField>

            <TextField v-model="mqttUsername" placeholder="Username">
              <template #FieldName>{{ $t('cloud_config.mqtt.broker_username') }}</template>
              <template #FieldDescription>{{ $t('cloud_config.mqtt.broker_username') }}</template>
            </TextField>

            <!-- TODO - Maybe make this an actual password field to prevent hypothetical shoulder surfing -->
            <TextField v-model="mqttPassword" placeholder="Password">
              <template #FieldName>{{ $t('cloud_config.mqtt.broker_password') }}</template>
              <template #FieldDescription>{{ $t('cloud_config.mqtt.broker_password') }}</template>
            </TextField>

            <TextField v-model="mqttTopic" placeholder="e.g. tiltbridge">
              <template #FieldName>{{ $t('cloud_config.mqtt.mqtt_topic') }}</template>
              <template #FieldDescription>{{ $t('cloud_config.mqtt.mqtt_topic_desc') }}</template>
            </TextField>

            <TextField v-model="mqttPushEvery" placeholder="30">
              <template #FieldName>{{ $t('cloud_config.mqtt.mqtt_push_every') }}</template>
              <template #FieldDescription>{{ $t('cloud_config.mqtt.mqtt_push_every_desc') }}</template>
            </TextField>

            <FormErrorMsg :form-error-message="form_error_message" v-if="form_error_message.length > 0" />
          </div>

          <div class="bg-white px-4 py-5 border-t border-gray-200 sm:px-6 sm:flex sm:flex-row-reverse">
            <button type="submit" class="inline-flex justify-center py-2 px-4 border border-transparent shadow-sm text-sm font-medium rounded-md text-white bg-blue-600 hover:bg-blue-700 focus:outline-none focus:ring-2 focus:ring-offset-2 focus:ring-blue-500">
              {{ $t('sitewide.update') }}
            </button>
          </div>

        </form>
      </div>
    </div>
  </div>

  <UpdateSuccessfulModal update-successful="update-successful" v-model="alertOpen" />
</template>

<script setup>
import { ref } from 'vue';
import { useConfigStore } from '@/stores/ConfigStore';
import TextField from '@/components/config/form_elements/TextField.vue';
import FormErrorMsg from '@/components/generic/FormErrorMsg.vue';
import SendTargetErrorMsg from "@/components/generic/SendTargetErrorMsg.vue";
import UpdateSuccessfulModal from '@/components/config/UpdateSuccessfulModal.vue';
import { useLoading } from 'vue-loading-overlay';

const configStore = useConfigStore();
const mqttBrokerHost = ref(configStore.mqttBrokerHost);
const mqttBrokerPort = ref(configStore.mqttBrokerPort);
const mqttUsername = ref(configStore.mqttUsername);
const mqttPassword = ref(configStore.mqttPassword);
const mqttTopic = ref(configStore.mqttTopic);
const mqttPushEvery = ref(configStore.mqttPushEvery);
let form_error_message = ref('');

const updateSuccessful = ref(false);
const alertOpen = ref(false);

const $loading = useLoading();

async function submitForm() {
  form_error_message.value = '';

  // TODO - Add validation checks here

  let loader = $loading.show({});

  await configStore.updateMQTTConfig(
      mqttBrokerHost.value,
      mqttBrokerPort.value,
      mqttUsername.value,
      mqttPassword.value,
      mqttTopic.value,
      mqttPushEvery.value
  );

  loader.hide();
  updateSuccessful.value = !configStore.configUpdateError; // configUpdateError is inverted from what we want here
  alertOpen.value = true;
}
</script>
