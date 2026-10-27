<script setup lang="ts">
defineProps<{
  status: 'loading' | 'scan' | 'error'
  qr: string
  message: string
}>()
const emit = defineEmits<{ (e: 'retry'): void }>()
</script>

<template>
  <div class="login">
    <div class="panel">
      <div class="head">
        <div class="logo">
          <svg width="26" height="26" viewBox="0 0 24 24" fill="none">
            <path d="M4 11.2 12 4l8 7.2V20H4v-8.8Z" stroke="currentColor" stroke-width="1.8" stroke-linejoin="round" />
            <path d="M9.5 20v-5.5h5V20" stroke="currentColor" stroke-width="1.8" stroke-linejoin="round" />
          </svg>
        </div>
        <h1>扫码登录米家</h1>
        <p>打开米家 App，扫一扫即可登录</p>
      </div>

      <div class="qr-slot">
        <div v-if="status === 'scan' && qr" class="qr-card">
          <img :src="qr" alt="登录二维码" />
        </div>
        <div v-else-if="status === 'loading'" class="placeholder">
          <span class="spinner" />
          <span>{{ message || '正在获取登录二维码…' }}</span>
        </div>
        <div v-else class="placeholder error">
          <span>{{ message || '获取二维码失败' }}</span>
          <button class="btn btn-primary" @click="emit('retry')">重试</button>
        </div>
      </div>

      <p class="tip">登录凭据只保存在本机，可用于下次免扫码</p>
    </div>
  </div>
</template>

<style scoped>
.login {
  height: 100%;
  display: grid;
  place-items: center;
  background:
    radial-gradient(900px 500px at 50% -10%, var(--brand-soft), transparent 70%),
    var(--bg);
}

.panel {
  width: 380px;
  padding: 34px 32px 26px;
  background: var(--surface);
  border: 1px solid var(--border);
  border-radius: var(--radius-xl);
  box-shadow: var(--shadow-md);
  text-align: center;
  animation: fade-up 0.28s ease both;
  position: relative;
  overflow: hidden;
}

.panel::before {
  content: '';
  position: absolute;
  inset: 0 0 auto 0;
  height: 3px;
  background: var(--grad-brand);
}

.head .logo {
  width: 54px;
  height: 54px;
  margin: 0 auto 14px;
  border-radius: 17px;
  background: var(--grad-brand);
  color: #fff;
  display: grid;
  place-items: center;
  box-shadow: var(--shadow-sm);
}

h1 {
  margin: 0 0 6px;
  font-size: 19px;
  font-weight: 600;
}

.head p {
  margin: 0;
  color: var(--text-2);
  font-size: 13px;
}

.qr-slot {
  margin: 24px 0 18px;
  display: grid;
  place-items: center;
  min-height: 248px;
}

.qr-card {
  padding: 12px;
  background: #fff;
  border: 1px solid var(--border);
  border-radius: var(--radius-md);
  box-shadow: var(--shadow-sm);
}

.qr-card img {
  display: block;
  width: 208px;
  height: 208px;
  image-rendering: pixelated;
}

.placeholder {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 14px;
  color: var(--text-2);
  font-size: 13px;
}

.placeholder.error {
  color: var(--danger);
}

.spinner {
  width: 26px;
  height: 26px;
  border-radius: 50%;
  border: 2.5px solid var(--border-strong);
  border-top-color: var(--brand);
  animation: spin 0.8s linear infinite;
}

@keyframes spin {
  to {
    transform: rotate(360deg);
  }
}

.tip {
  margin: 0;
  color: var(--text-3);
  font-size: 12px;
}
</style>
